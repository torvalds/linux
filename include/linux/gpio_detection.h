/*
 * include/linux/gpio_detection.h
 *
 * Platform data structure for GPIO detection driver
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef __GPIO_DETECTION_H
#define __GPIO_DETECTION_H

#define GPIO_EVENT		1

/*
 * gpio event
 *  @val: 0 event active, 1 event over
 *  @name: event name
 */

struct gpio_event {
	int val;
	const char *name;
};

#if IS_ENABLED(CONFIG_GPIO_DET)

int gpio_det_register_notifier(struct notifier_block *nb);
int gpio_det_unregister_notifier(struct notifier_block *nb);
int gpio_det_notifier_call_chain(unsigned long val, void *v);

#else

static inline int gpio_det_register_notifier(struct notifier_block *nb)
{
	return -EINVAL;
};

static inline int gpio_det_unregister_notifier(struct notifier_block *nb)
{
	return -EINVAL;
};

static inline int gpio_det_notifier_call_chain(unsigned long val, void *v)
{
	return -EINVAL;
};

#endif

#endif
