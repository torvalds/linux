/* Public domain. */

#ifndef _LINUX_DEVICE_BUS_H
#define _LINUX_DEVICE_BUS_H

struct bus_type {
};

struct notifier_block;

static inline int
bus_register_notifier(const struct bus_type *bt, struct notifier_block *nb)
{
	return 0;
}

static inline int
bus_unregister_notifier(const struct bus_type *bt, struct notifier_block *nb)
{
	return 0;
}

#endif
