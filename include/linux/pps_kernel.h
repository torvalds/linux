/*
 * PPS API kernel header
 *
 * Copyright (C) 2009   Rodolfo Giometti <giometti@linux.it>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef LINUX_PPS_KERNEL_H
#define LINUX_PPS_KERNEL_H

#include <linux/pps.h>

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/time.h>

/*
 * Global defines
 */

struct pps_device;

/* The specific PPS source info */
struct pps_source_info {
	char name[PPS_MAX_NAME_LEN];		/* simbolic name */
	char path[PPS_MAX_NAME_LEN];		/* path of connected device */
	int mode;				/* PPS's allowed mode */

	void (*echo)(struct pps_device *pps,
			int event, void *data);	/* PPS echo function */

	struct module *owner;
	struct device *dev;		/* Parent device for device_create */
};

struct pps_event_time {
#ifdef CONFIG_NTP_PPS
	struct timespec ts_raw;
#endif /* CONFIG_NTP_PPS */
	struct timespec ts_real;
};

/* The main struct */
struct pps_device {
	struct pps_source_info info;		/* PSS source info */

	struct pps_kparams params;		/* PPS's current params */

	__u32 assert_sequence;			/* PPS' assert event seq # */
	__u32 clear_sequence;			/* PPS' clear event seq # */
	struct pps_ktime assert_tu;
	struct pps_ktime clear_tu;
	int current_mode;			/* PPS mode at event time */

	unsigned int last_ev;			/* last PPS event id */
	wait_queue_head_t queue;		/* PPS event queue */

	unsigned int id;			/* PPS source unique ID */
	void const *lookup_cookie;		/* pps_lookup_dev only */
	struct cdev cdev;
	struct device *dev;
	struct fasync_struct *async_queue;	/* fasync method */
	spinlock_t lock;
};

/*
 * Global variables
 */

extern struct device_attribute pps_attrs[];

/*
 * Internal functions.
 *
 * These are not actually part of the exported API, but this is a
 * convenient header file to put them in.
 */

extern int pps_register_cdev(struct pps_device *pps);
extern void pps_unregister_cdev(struct pps_device *pps);

/*
 * Exported functions
 */

extern struct pps_device *pps_register_source(
		struct pps_source_info *info, int default_params);
extern void pps_unregister_source(struct pps_device *pps);
extern void pps_event(struct pps_device *pps,
		struct pps_event_time *ts, int event, void *data);
/* Look up a pps device by magic cookie */
struct pps_device *pps_lookup_dev(void const *cookie);

static inline void timespec_to_pps_ktime(struct pps_ktime *kt,
		struct timespec ts)
{
	kt->sec = ts.tv_sec;
	kt->nsec = ts.tv_nsec;
}

#ifdef CONFIG_NTP_PPS

static inline void pps_get_ts(struct pps_event_time *ts)
{
	getnstime_raw_and_real(&ts->ts_raw, &ts->ts_real);
}

#else /* CONFIG_NTP_PPS */

static inline void pps_get_ts(struct pps_event_time *ts)
{
	getnstimeofday(&ts->ts_real);
}

#endif /* CONFIG_NTP_PPS */

#endif /* LINUX_PPS_KERNEL_H */

