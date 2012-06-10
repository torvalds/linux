#ifndef __UHID_H_
#define __UHID_H_

/*
 * User-space I/O driver support for HID subsystem
 * Copyright (c) 2012 David Herrmann
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * Public header for user-space communication. We try to keep every structure
 * aligned but to be safe we also use __attribute__((__packed__)). Therefore,
 * the communication should be ABI compatible even between architectures.
 */

#include <linux/input.h>
#include <linux/types.h>

enum uhid_event_type {
	UHID_DUMMY,
};

struct uhid_event {
	__u32 type;
} __attribute__((__packed__));

#endif /* __UHID_H_ */
