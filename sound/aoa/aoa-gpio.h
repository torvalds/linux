/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Apple Onboard Audio GPIO definitions
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 */

#ifndef __AOA_GPIO_H
#define __AOA_GPIO_H
#include <linux/workqueue.h>
#include <linux/mutex.h>

typedef void (*notify_func_t)(void *data);

enum notify_type {
	AOA_NOTIFY_HEADPHONE,
	AOA_NOTIFY_LINE_IN,
	AOA_NOTIFY_LINE_OUT,
};

struct gpio_runtime;
struct gpio_methods {
	/* for initialisation/de-initialisation of the GPIO layer */
	void (*init)(struct gpio_runtime *rt);
	void (*exit)(struct gpio_runtime *rt);

	/* turn off headphone, speakers, lineout */
	void (*all_amps_off)(struct gpio_runtime *rt);
	/* turn headphone, speakers, lineout back to previous setting */
	void (*all_amps_restore)(struct gpio_runtime *rt);

	void (*set_headphone)(struct gpio_runtime *rt, int on);
	void (*set_speakers)(struct gpio_runtime *rt, int on);
	void (*set_lineout)(struct gpio_runtime *rt, int on);
	void (*set_master)(struct gpio_runtime *rt, int on);

	int (*get_headphone)(struct gpio_runtime *rt);
	int (*get_speakers)(struct gpio_runtime *rt);
	int (*get_lineout)(struct gpio_runtime *rt);
	int (*get_master)(struct gpio_runtime *rt);

	void (*set_hw_reset)(struct gpio_runtime *rt, int on);

	/* use this to be notified of any events. The notification
	 * function is passed the data, and is called in process
	 * context by the use of schedule_work.
	 * The interface for it is that setting a function to NULL
	 * removes it, and they return 0 if the operation succeeded,
	 * and -EBUSY if the notification is already assigned by
	 * someone else. */
	int (*set_notify)(struct gpio_runtime *rt,
			  enum notify_type type,
			  notify_func_t notify,
			  void *data);
	/* returns 0 if not plugged in, 1 if plugged in
	 * or a negative error code */
	int (*get_detect)(struct gpio_runtime *rt,
			  enum notify_type type);
};

struct gpio_notification {
	struct delayed_work work;
	notify_func_t notify;
	void *data;
	void *gpio_private;
	struct mutex mutex;
};

struct gpio_runtime {
	/* to be assigned by fabric */
	struct device_node *node;
	/* since everyone needs this pointer anyway... */
	struct gpio_methods *methods;
	/* to be used by the gpio implementation */
	int implementation_private;
	struct gpio_notification headphone_notify;
	struct gpio_notification line_in_notify;
	struct gpio_notification line_out_notify;
};

#endif /* __AOA_GPIO_H */
