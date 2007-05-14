#ifndef _HIDRAW_H
#define _HIDRAW_H

/*
 *  Copyright (c) 2007 Jiri Kosina
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

struct hidraw_report_descriptor {
	__u32 size;
	__u8 *value;
};

struct hidraw_devinfo {
	__u32 bustype;
	__s16 vendor;
	__s16 product;
};

/* ioctl interface */
#define HIDIOCGRDESCSIZE	_IOR('H', 0x01, int)
#define HIDIOCGRDESC		_IOR('H', 0x02, struct hidraw_report_descriptor)
#define HIDIOCGRAWINFO		_IOR('H', 0x03, struct hidraw_devinfo)

#define HIDRAW_FIRST_MINOR 0
#define HIDRAW_MAX_DEVICES 64
/* number of reports to buffer */
#define HIDRAW_BUFFER_SIZE 64


/* kernel-only API declarations */
#ifdef __KERNEL__

#include <linux/hid.h>

struct hidraw {
	unsigned int minor;
	int exist;
	int open;
	wait_queue_head_t wait;
	struct hid_device *hid;
	struct device *dev;
	struct list_head list;
};

struct hidraw_report {
	__u8 *value;
	int len;
};

struct hidraw_list {
	struct hidraw_report buffer[HIDRAW_BUFFER_SIZE];
	int head;
	int tail;
	struct fasync_struct *fasync;
	struct hidraw *hidraw;
	struct list_head node;
	struct mutex read_mutex;
};

#ifdef CONFIG_HIDRAW
int hidraw_init(void);
void hidraw_exit(void);
void hidraw_report_event(struct hid_device *, u8 *, int);
int hidraw_connect(struct hid_device *);
void hidraw_disconnect(struct hid_device *);
#else
static inline int hidraw_init(void) { return 0; }
static inline void hidraw_exit(void) { }
static inline void hidraw_report_event(struct hid_device *hid, u8 *data, int len) { }
static inline int hidraw_connect(struct hid_device *hid) { return -1; }
static inline void hidraw_disconnect(struct hid_device *hid) { }
#endif

#endif

#endif
