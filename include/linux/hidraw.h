/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (c) 2007 Jiri Kosina
 */
#ifndef _HIDRAW_H
#define _HIDRAW_H

#include <uapi/linux/hidraw.h>


struct hidraw {
	unsigned int minor;
	int exist;
	int open;
	wait_queue_head_t wait;
	struct hid_device *hid;
	struct device *dev;
	spinlock_t list_lock;
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
	bool revoked;
};

#ifdef CONFIG_HIDRAW
int hidraw_init(void);
void hidraw_exit(void);
int hidraw_report_event(struct hid_device *, u8 *, int);
int hidraw_connect(struct hid_device *);
void hidraw_disconnect(struct hid_device *);
#else
static inline int hidraw_init(void) { return 0; }
static inline void hidraw_exit(void) { }
static inline int hidraw_report_event(struct hid_device *hid, u8 *data, int len) { return 0; }
static inline int hidraw_connect(struct hid_device *hid) { return -1; }
static inline void hidraw_disconnect(struct hid_device *hid) { }
#endif

#endif
