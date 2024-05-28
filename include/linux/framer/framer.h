/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic framer header file
 *
 * Copyright 2023 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#ifndef __DRIVERS_FRAMER_H
#define __DRIVERS_FRAMER_H

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/workqueue.h>

/**
 * enum framer_iface - Framer interface
 * @FRAMER_IFACE_E1: E1 interface
 * @FRAMER_IFACE_T1: T1 interface
 */
enum framer_iface {
	FRAMER_IFACE_E1,
	FRAMER_IFACE_T1,
};

/**
 * enum framer_clock_type - Framer clock type
 * @FRAMER_CLOCK_EXT: External clock
 * @FRAMER_CLOCK_INT: Internal clock
 */
enum framer_clock_type {
	FRAMER_CLOCK_EXT,
	FRAMER_CLOCK_INT,
};

/**
 * struct framer_config - Framer configuration
 * @iface: Framer line interface
 * @clock_type: Framer clock type
 * @line_clock_rate: Framer line clock rate
 */
struct framer_config {
	enum framer_iface iface;
	enum framer_clock_type clock_type;
	unsigned long line_clock_rate;
};

/**
 * struct framer_status - Framer status
 * @link_is_on: Framer link state. true, the link is on, false, the link is off.
 */
struct framer_status {
	bool link_is_on;
};

/**
 * enum framer_event - Event available for notification
 * @FRAMER_EVENT_STATUS: Event notified on framer_status changes
 */
enum framer_event {
	FRAMER_EVENT_STATUS,
};

/**
 * struct framer - represents the framer device
 * @dev: framer device
 * @id: id of the framer device
 * @ops: function pointers for performing framer operations
 * @mutex: mutex to protect framer_ops
 * @init_count: used to protect when the framer is used by multiple consumers
 * @power_count: used to protect when the framer is used by multiple consumers
 * @pwr: power regulator associated with the framer
 * @notify_status_work: work structure used for status notifications
 * @notifier_list: notifier list used for notifications
 * @polling_work: delayed work structure used for the polling task
 * @prev_status: previous read status used by the polling task to detect changes
 */
struct framer {
	struct device			dev;
	int				id;
	const struct framer_ops		*ops;
	struct mutex			mutex;	/* Protect framer */
	int				init_count;
	int				power_count;
	struct regulator		*pwr;
	struct work_struct		notify_status_work;
	struct blocking_notifier_head	notifier_list;
	struct delayed_work		polling_work;
	struct framer_status		prev_status;
};

#if IS_ENABLED(CONFIG_GENERIC_FRAMER)
int framer_pm_runtime_get(struct framer *framer);
int framer_pm_runtime_get_sync(struct framer *framer);
int framer_pm_runtime_put(struct framer *framer);
int framer_pm_runtime_put_sync(struct framer *framer);
int framer_init(struct framer *framer);
int framer_exit(struct framer *framer);
int framer_power_on(struct framer *framer);
int framer_power_off(struct framer *framer);
int framer_get_status(struct framer *framer, struct framer_status *status);
int framer_get_config(struct framer *framer, struct framer_config *config);
int framer_set_config(struct framer *framer, const struct framer_config *config);
int framer_notifier_register(struct framer *framer, struct notifier_block *nb);
int framer_notifier_unregister(struct framer *framer, struct notifier_block *nb);

struct framer *framer_get(struct device *dev, const char *con_id);
void framer_put(struct device *dev, struct framer *framer);

struct framer *devm_framer_get(struct device *dev, const char *con_id);
struct framer *devm_framer_optional_get(struct device *dev, const char *con_id);
#else
static inline int framer_pm_runtime_get(struct framer *framer)
{
	return -ENOSYS;
}

static inline int framer_pm_runtime_get_sync(struct framer *framer)
{
	return -ENOSYS;
}

static inline int framer_pm_runtime_put(struct framer *framer)
{
	return -ENOSYS;
}

static inline int framer_pm_runtime_put_sync(struct framer *framer)
{
	return -ENOSYS;
}

static inline int framer_init(struct framer *framer)
{
	return -ENOSYS;
}

static inline int framer_exit(struct framer *framer)
{
	return -ENOSYS;
}

static inline int framer_power_on(struct framer *framer)
{
	return -ENOSYS;
}

static inline int framer_power_off(struct framer *framer)
{
	return -ENOSYS;
}

static inline int framer_get_status(struct framer *framer, struct framer_status *status)
{
	return -ENOSYS;
}

static inline int framer_get_config(struct framer *framer, struct framer_config *config)
{
	return -ENOSYS;
}

static inline int framer_set_config(struct framer *framer, const struct framer_config *config)
{
	return -ENOSYS;
}

static inline int framer_notifier_register(struct framer *framer,
					   struct notifier_block *nb)
{
	return -ENOSYS;
}

static inline int framer_notifier_unregister(struct framer *framer,
					     struct notifier_block *nb)
{
	return -ENOSYS;
}

static inline struct framer *framer_get(struct device *dev, const char *con_id)
{
	return ERR_PTR(-ENOSYS);
}

static inline void framer_put(struct device *dev, struct framer *framer)
{
}

static inline struct framer *devm_framer_get(struct device *dev, const char *con_id)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct framer *devm_framer_optional_get(struct device *dev, const char *con_id)
{
	return NULL;
}

#endif

#endif /* __DRIVERS_FRAMER_H */
