/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PPS generator API kernel header
 *
 * Copyright (C) 2024 Rodolfo Giometti <giometti@enneenne.com>
 */

#ifndef LINUX_PPS_GEN_KERNEL_H
#define LINUX_PPS_GEN_KERNEL_H

#include <linux/pps_gen.h>
#include <linux/cdev.h>
#include <linux/device.h>

/*
 * Global defines
 */

#define PPS_GEN_MAX_SOURCES	16		/* should be enough... */

struct pps_gen_device;

/**
 * struct pps_gen_source_info - the specific PPS generator info
 * @use_system_clock: true, if the system clock is used to generate pulses
 * @get_time: query the time stored into the generator clock
 * @enable: enable/disable the PPS pulses generation
 *
 * This is the main generator struct where all needed information must be
 * placed before calling the pps_gen_register_source().
 */
struct pps_gen_source_info {
	bool use_system_clock;

	int (*get_time)(struct pps_gen_device *pps_gen,
					struct timespec64 *time);
	int (*enable)(struct pps_gen_device *pps_gen, bool enable);

/* private: internal use only */
	struct module *owner;
	struct device *parent;			/* for device_create */
};

/* The main struct */
struct pps_gen_device {
	const struct pps_gen_source_info *info;	/* PSS generator info */
	bool enabled;				/* PSS generator status */

	unsigned int event;
	unsigned int sequence;

	unsigned int last_ev;			/* last PPS event id */
	wait_queue_head_t queue;		/* PPS event queue */

	unsigned int id;			/* PPS generator unique ID */
	struct cdev cdev;
	struct device *dev;
	struct fasync_struct *async_queue;	/* fasync method */
	spinlock_t lock;
};

/*
 * Global variables
 */

extern const struct attribute_group *pps_gen_groups[];

/*
 * Exported functions
 */

extern struct pps_gen_device *pps_gen_register_source(
				const struct pps_gen_source_info *info);
extern void pps_gen_unregister_source(struct pps_gen_device *pps_gen);
extern void pps_gen_event(struct pps_gen_device *pps_gen,
				unsigned int event, void *data);

#endif /* LINUX_PPS_GEN_KERNEL_H */
