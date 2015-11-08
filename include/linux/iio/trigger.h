/* The industrial I/O core, trigger handling functions
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/atomic.h>

#ifndef _IIO_TRIGGER_H_
#define _IIO_TRIGGER_H_

#ifdef CONFIG_IIO_TRIGGER
struct iio_subirq {
	bool enabled;
};

struct iio_dev;
struct iio_trigger;

/**
 * struct iio_trigger_ops - operations structure for an iio_trigger.
 * @owner:		used to monitor usage count of the trigger.
 * @set_trigger_state:	switch on/off the trigger on demand
 * @try_reenable:	function to reenable the trigger when the
 *			use count is zero (may be NULL)
 * @validate_device:	function to validate the device when the
 *			current trigger gets changed.
 *
 * This is typically static const within a driver and shared by
 * instances of a given device.
 **/
struct iio_trigger_ops {
	struct module *owner;
	int (*set_trigger_state)(struct iio_trigger *trig, bool state);
	int (*try_reenable)(struct iio_trigger *trig);
	int (*validate_device)(struct iio_trigger *trig,
			       struct iio_dev *indio_dev);
};


/**
 * struct iio_trigger - industrial I/O trigger device
 * @ops:		[DRIVER] operations structure
 * @id:			[INTERN] unique id number
 * @name:		[DRIVER] unique name
 * @dev:		[DRIVER] associated device (if relevant)
 * @list:		[INTERN] used in maintenance of global trigger list
 * @alloc_list:		[DRIVER] used for driver specific trigger list
 * @use_count:		use count for the trigger
 * @subirq_chip:	[INTERN] associate 'virtual' irq chip.
 * @subirq_base:	[INTERN] base number for irqs provided by trigger.
 * @subirqs:		[INTERN] information about the 'child' irqs.
 * @pool:		[INTERN] bitmap of irqs currently in use.
 * @pool_lock:		[INTERN] protection of the irq pool.
 **/
struct iio_trigger {
	const struct iio_trigger_ops	*ops;
	int				id;
	const char			*name;
	struct device			dev;

	struct list_head		list;
	struct list_head		alloc_list;
	atomic_t			use_count;

	struct irq_chip			subirq_chip;
	int				subirq_base;

	struct iio_subirq subirqs[CONFIG_IIO_CONSUMERS_PER_TRIGGER];
	unsigned long pool[BITS_TO_LONGS(CONFIG_IIO_CONSUMERS_PER_TRIGGER)];
	struct mutex			pool_lock;
};


static inline struct iio_trigger *to_iio_trigger(struct device *d)
{
	return container_of(d, struct iio_trigger, dev);
}

static inline void iio_trigger_put(struct iio_trigger *trig)
{
	module_put(trig->ops->owner);
	put_device(&trig->dev);
}

static inline struct iio_trigger *iio_trigger_get(struct iio_trigger *trig)
{
	get_device(&trig->dev);
	__module_get(trig->ops->owner);

	return trig;
}

/**
 * iio_device_set_drvdata() - Set trigger driver data
 * @trig: IIO trigger structure
 * @data: Driver specific data
 *
 * Allows to attach an arbitrary pointer to an IIO trigger, which can later be
 * retrieved by iio_trigger_get_drvdata().
 */
static inline void iio_trigger_set_drvdata(struct iio_trigger *trig, void *data)
{
	dev_set_drvdata(&trig->dev, data);
}

/**
 * iio_trigger_get_drvdata() - Get trigger driver data
 * @trig: IIO trigger structure
 *
 * Returns the data previously set with iio_trigger_set_drvdata()
 */
static inline void *iio_trigger_get_drvdata(struct iio_trigger *trig)
{
	return dev_get_drvdata(&trig->dev);
}

/**
 * iio_trigger_register() - register a trigger with the IIO core
 * @trig_info:	trigger to be registered
 **/
int iio_trigger_register(struct iio_trigger *trig_info);

/**
 * iio_trigger_unregister() - unregister a trigger from the core
 * @trig_info:	trigger to be unregistered
 **/
void iio_trigger_unregister(struct iio_trigger *trig_info);

/**
 * iio_trigger_poll() - called on a trigger occurring
 * @trig:	trigger which occurred
 *
 * Typically called in relevant hardware interrupt handler.
 **/
void iio_trigger_poll(struct iio_trigger *trig);
void iio_trigger_poll_chained(struct iio_trigger *trig);

irqreturn_t iio_trigger_generic_data_rdy_poll(int irq, void *private);

__printf(1, 2) struct iio_trigger *iio_trigger_alloc(const char *fmt, ...);
void iio_trigger_free(struct iio_trigger *trig);

#else
struct iio_trigger;
struct iio_trigger_ops;
#endif
#endif /* _IIO_TRIGGER_H_ */
