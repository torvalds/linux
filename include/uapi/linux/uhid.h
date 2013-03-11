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
	UHID_START,
	UHID_STOP,
	UHID_OPEN,
	UHID_CLOSE,
	UHID_OUTPUT,
	UHID_OUTPUT_EV,
	UHID_INPUT,
	UHID_FEATURE,
	UHID_FEATURE_ANSWER,
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

enum uhid_report_type {
	UHID_FEATURE_REPORT,
	UHID_OUTPUT_REPORT,
	UHID_INPUT_REPORT,
};

struct uhid_input_req {
	__u8 data[UHID_DATA_MAX];
	__u16 size;
} __attribute__((__packed__));

struct uhid_output_req {
	__u8 data[UHID_DATA_MAX];
	__u16 size;
	__u8 rtype;
} __attribute__((__packed__));

struct uhid_output_ev_req {
	__u16 type;
	__u16 code;
	__s32 value;
} __attribute__((__packed__));

struct uhid_feature_req {
	__u32 id;
	__u8 rnum;
	__u8 rtype;
} __attribute__((__packed__));

struct uhid_feature_answer_req {
	__u32 id;
	__u16 err;
	__u16 size;
	__u8 data[UHID_DATA_MAX];
} __attribute__((__packed__));

struct uhid_event {
	__u32 type;

	union {
		struct uhid_create_req create;
		struct uhid_input_req input;
		struct uhid_output_req output;
		struct uhid_output_ev_req output_ev;
		struct uhid_feature_req feature;
		struct uhid_feature_answer_req feature_answer;
	} u;
} __attribute__((__packed__));

#endif /* __UHID_H_ */
