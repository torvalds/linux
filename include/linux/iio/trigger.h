/* SPDX-License-Identifier: GPL-2.0-only */
/* The industrial I/O core, trigger handling functions
 *
 * Copyright (c) 2008 Jonathan Cameron
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
 * @set_trigger_state:	switch on/off the trigger on demand
 * @reenable:		function to reenable the trigger when the
 *			use count is zero (may be NULL)
 * @validate_device:	function to validate the device when the
 *			current trigger gets changed.
 *
 * This is typically static const within a driver and shared by
 * instances of a given device.
 **/
struct iio_trigger_ops {
	int (*set_trigger_state)(struct iio_trigger *trig, bool state);
	void (*reenable)(struct iio_trigger *trig);
	int (*validate_device)(struct iio_trigger *trig,
			       struct iio_dev *indio_dev);
};


/**
 * struct iio_trigger - industrial I/O trigger device
 * @ops:		[DRIVER] operations structure
 * @owner:		[INTERN] owner of this driver module
 * @id:			[INTERN] unique id number
 * @name:		[DRIVER] unique name
 * @dev:		[DRIVER] associated device (if relevant)
 * @list:		[INTERN] used in maintenance of global trigger list
 * @alloc_list:		[DRIVER] used for driver specific trigger list
 * @use_count:		[INTERN] use count for the trigger.
 * @subirq_chip:	[INTERN] associate 'virtual' irq chip.
 * @subirq_base:	[INTERN] base number for irqs provided by trigger.
 * @subirqs:		[INTERN] information about the 'child' irqs.
 * @pool:		[INTERN] bitmap of irqs currently in use.
 * @pool_lock:		[INTERN] protection of the irq pool.
 * @attached_own_device:[INTERN] if we are using our own device as trigger,
 *			i.e. if we registered a poll function to the same
 *			device as the one providing the trigger.
 * @reenable_work:	[INTERN] work item used to ensure reenable can sleep.
 **/
struct iio_trigger {
	const struct iio_trigger_ops	*ops;
	struct module			*owner;
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
	bool				attached_own_device;
	struct work_struct		reenable_work;
};


static inline struct iio_trigger *to_iio_trigger(struct device *d)
{
	return container_of(d, struct iio_trigger, dev);
}

static inline void iio_trigger_put(struct iio_trigger *trig)
{
	module_put(trig->owner);
	put_device(&trig->dev);
}

static inline struct iio_trigger *iio_trigger_get(struct iio_trigger *trig)
{
	get_device(&trig->dev);

	WARN_ONCE(list_empty(&trig->list),
		  "Getting non-registered iio trigger %s is prohibited\n",
		  trig->name);

	__module_get(trig->owner);

	return trig;
}

/**
 * iio_trigger_set_drvdata() - Set trigger driver data
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

int devm_iio_trigger_register(struct device *dev,
			      struct iio_trigger *trig_info);

/**
 * iio_trigger_unregister() - unregister a trigger from the core
 * @trig_info:	trigger to be unregistered
 **/
void iio_trigger_unregister(struct iio_trigger *trig_info);

/**
 * iio_trigger_set_immutable() - set an immutable trigger on destination
 *
 * @indio_dev: IIO device structure containing the device
 * @trig: trigger to assign to device
 *
 **/
int iio_trigger_set_immutable(struct iio_dev *indio_dev, struct iio_trigger *trig);

void iio_trigger_poll(struct iio_trigger *trig);
void iio_trigger_poll_nested(struct iio_trigger *trig);

irqreturn_t iio_trigger_generic_data_rdy_poll(int irq, void *private);

#define iio_trigger_alloc(parent, fmt, ...) \
	__iio_trigger_alloc((parent), THIS_MODULE, (fmt), ##__VA_ARGS__)

__printf(3, 4)
struct iio_trigger *__iio_trigger_alloc(struct device *parent,
					struct module *this_mod,
					const char *fmt, ...);
void iio_trigger_free(struct iio_trigger *trig);

/**
 * iio_trigger_using_own() - tells us if we use our own HW trigger ourselves
 * @indio_dev:  device to check
 */
bool iio_trigger_using_own(struct iio_dev *indio_dev);

int iio_validate_own_trigger(struct iio_dev *idev, struct iio_trigger *trig);
int iio_trigger_validate_own_device(struct iio_trigger *trig,
				     struct iio_dev *indio_dev);

#else
struct iio_trigger;
struct iio_trigger_ops;
#endif
#endif /* _IIO_TRIGGER_H_ */
