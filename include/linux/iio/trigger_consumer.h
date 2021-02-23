/* SPDX-License-Identifier: GPL-2.0-only */
/* The industrial I/O core, trigger consumer functions
 *
 * Copyright (c) 2008-2011 Jonathan Cameron
 */

#ifndef __LINUX_IIO_TRIGGER_CONSUMER_H__
#define __LINUX_IIO_TRIGGER_CONSUMER_H__

#include <linux/interrupt.h>
#include <linux/types.h>

struct iio_dev;
struct iio_trigger;

/**
 * struct iio_poll_func - poll function pair
 *
 * @indio_dev:			data specific to device (passed into poll func)
 * @h:				the function that is actually run on trigger
 * @thread:			threaded interrupt part
 * @type:			the type of interrupt (basically if oneshot)
 * @name:			name used to identify the trigger consumer.
 * @irq:			the corresponding irq as allocated from the
 *				trigger pool
 * @timestamp:			some devices need a timestamp grabbed as soon
 *				as possible after the trigger - hence handler
 *				passes it via here.
 **/
struct iio_poll_func {
	struct iio_dev *indio_dev;
	irqreturn_t (*h)(int irq, void *p);
	irqreturn_t (*thread)(int irq, void *p);
	int type;
	char *name;
	int irq;
	s64 timestamp;
};


__printf(5, 6) struct iio_poll_func
*iio_alloc_pollfunc(irqreturn_t (*h)(int irq, void *p),
		    irqreturn_t (*thread)(int irq, void *p),
		    int type,
		    struct iio_dev *indio_dev,
		    const char *fmt,
		    ...);
void iio_dealloc_pollfunc(struct iio_poll_func *pf);
irqreturn_t iio_pollfunc_store_time(int irq, void *p);

void iio_trigger_notify_done(struct iio_trigger *trig);

#endif
