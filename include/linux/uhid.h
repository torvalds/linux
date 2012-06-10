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
	UHID_CREATE,
	UHID_DESTROY,
	UHID_INPUT,
};

struct uhid_create_req {
	__u8 name[128];
	__u8 phys[64];
	__u8 uniq[64];
	__u8 __user *rd_data;
	__u16 rd_size;

	__u16 bus;
	__u32 vendor;
	__u32 product;
	__u32 version;
	__u32 country;
} __attribute__((__packed__));

#define UHID_DATA_MAX 4096

struct uhid_input_req {
	__u8 data[UHID_DATA_MAX];
	__u16 size;
} __attribute__((__packed__));

struct uhid_event {
	__u32 type;

	union {
		struct uhid_create_req create;
		struct uhid_input_req input;
	} u;
} __attribute__((__packed__));

#endif /* __UHID_H_ */
