/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * <linux/gpio.h> - userspace ABI for the GPIO character devices
 *
 * Copyright (C) 2016 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef _UAPI_GPIO_H_
#define _UAPI_GPIO_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct gpiochip_info - Information about a certain GPIO chip
 * @name: the Linux kernel name of this GPIO chip
 * @label: a functional name for this GPIO chip, such as a product
 * number, may be NULL
 * @lines: number of GPIO lines on this chip
 */
struct gpiochip_info {
	char name[32];
	char label[32];
	__u32 lines;
};

/* Informational flags */
#define GPIOLINE_FLAG_KERNEL		(1UL << 0) /* Line used by the kernel */
#define GPIOLINE_FLAG_IS_OUT		(1UL << 1)
#define GPIOLINE_FLAG_ACTIVE_LOW	(1UL << 2)
#define GPIOLINE_FLAG_OPEN_DRAIN	(1UL << 3)
#define GPIOLINE_FLAG_OPEN_SOURCE	(1UL << 4)
#define GPIOLINE_FLAG_PULL_UP	(1UL << 5)
#define GPIOLINE_FLAG_PULL_DOWN	(1UL << 6)

/**
 * struct gpioline_info - Information about a certain GPIO line
 * @line_offset: the local offset on this GPIO device, fill this in when
 * requesting the line information from the kernel
 * @flags: various flags for this line
 * @name: the name of this GPIO line, such as the output pin of the line on the
 * chip, a rail or a pin header name on a board, as specified by the gpio
 * chip, may be NULL
 * @consumer: a functional name for the consumer of this GPIO line as set by
 * whatever is using it, will be NULL if there is no current user but may
 * also be NULL if the consumer doesn't set this up
 */
struct gpioline_info {
	__u32 line_offset;
	__u32 flags;
	char name[32];
	char consumer[32];
};

/* Maximum number of requested handles */
#define GPIOHANDLES_MAX 64

/* Linerequest flags */
#define GPIOHANDLE_REQUEST_INPUT	(1UL << 0)
#define GPIOHANDLE_REQUEST_OUTPUT	(1UL << 1)
#define GPIOHANDLE_REQUEST_ACTIVE_LOW	(1UL << 2)
#define GPIOHANDLE_REQUEST_OPEN_DRAIN	(1UL << 3)
#define GPIOHANDLE_REQUEST_OPEN_SOURCE	(1UL << 4)
#define GPIOHANDLE_REQUEST_PULL_UP	(1UL << 5)
#define GPIOHANDLE_REQUEST_PULL_DOWN	(1UL << 6)

/**
 * struct gpiohandle_request - Information about a GPIO handle request
 * @lineoffsets: an array of desired lines, specified by offset index for the
 * associated GPIO device
 * @flags: desired flags for the desired GPIO lines, such as
 * GPIOHANDLE_REQUEST_OUTPUT, GPIOHANDLE_REQUEST_ACTIVE_LOW etc, OR:ed
 * together. Note that even if multiple lines are requested, the same flags
 * must be applicable to all of them, if you want lines with individual
 * flags set, request them one by one. It is possible to select
 * a batch of input or output lines, but they must all have the same
 * characteristics, i.e. all inputs or all outputs, all active low etc
 * @default_values: if the GPIOHANDLE_REQUEST_OUTPUT is set for a requested
 * line, this specifies the default output value, should be 0 (low) or
 * 1 (high), anything else than 0 or 1 will be interpreted as 1 (high)
 * @consumer_label: a desired consumer label for the selected GPIO line(s)
 * such as "my-bitbanged-relay"
 * @lines: number of lines requested in this request, i.e. the number of
 * valid fields in the above arrays, set to 1 to request a single line
 * @fd: if successful this field will contain a valid anonymous file handle
 * after a GPIO_GET_LINEHANDLE_IOCTL operation, zero or negative value
 * means error
 */
struct gpiohandle_request {
	__u32 lineoffsets[GPIOHANDLES_MAX];
	__u32 flags;
	__u8 default_values[GPIOHANDLES_MAX];
	char consumer_label[32];
	__u32 lines;
	int fd;
};

/**
 * struct gpiohandle_data - Information of values on a GPIO handle
 * @values: when getting the state of lines this contains the current
 * state of a line, when setting the state of lines these should contain
 * the desired target state
 */
struct gpiohandle_data {
	__u8 values[GPIOHANDLES_MAX];
};

#define GPIOHANDLE_GET_LINE_VALUES_IOCTL _IOWR(0xB4, 0x08, struct gpiohandle_data)
#define GPIOHANDLE_SET_LINE_VALUES_IOCTL _IOWR(0xB4, 0x09, struct gpiohandle_data)

/* Eventrequest flags */
#define GPIOEVENT_REQUEST_RISING_EDGE	(1UL << 0)
#define GPIOEVENT_REQUEST_FALLING_EDGE	(1UL << 1)
#define GPIOEVENT_REQUEST_BOTH_EDGES	((1UL << 0) | (1UL << 1))

/**
 * struct gpioevent_request - Information about a GPIO event request
 * @lineoffset: the desired line to subscribe to events from, specified by
 * offset index for the associated GPIO device
 * @handleflags: desired handle flags for the desired GPIO line, such as
 * GPIOHANDLE_REQUEST_ACTIVE_LOW or GPIOHANDLE_REQUEST_OPEN_DRAIN
 * @eventflags: desired flags for the desired GPIO event line, such as
 * GPIOEVENT_REQUEST_RISING_EDGE or GPIOEVENT_REQUEST_FALLING_EDGE
 * @consumer_label: a desired consumer label for the selected GPIO line(s)
 * such as "my-listener"
 * @fd: if successful this field will contain a valid anonymous file handle
 * after a GPIO_GET_LINEEVENT_IOCTL operation, zero or negative value
 * means error
 */
struct gpioevent_request {
	__u32 lineoffset;
	__u32 handleflags;
	__u32 eventflags;
	char consumer_label[32];
	int fd;
};

/**
 * GPIO event types
 */
#define GPIOEVENT_EVENT_RISING_EDGE 0x01
#define GPIOEVENT_EVENT_FALLING_EDGE 0x02

/**
 * struct gpioevent_data - The actual event being pushed to userspace
 * @timestamp: best estimate of time of event occurrence, in nanoseconds
 * @id: event identifier
 */
struct gpioevent_data {
	__u64 timestamp;
	__u32 id;
};

#define GPIO_GET_CHIPINFO_IOCTL _IOR(0xB4, 0x01, struct gpiochip_info)
#define GPIO_GET_LINEINFO_IOCTL _IOWR(0xB4, 0x02, struct gpioline_info)
#define GPIO_GET_LINEHANDLE_IOCTL _IOWR(0xB4, 0x03, struct gpiohandle_request)
#define GPIO_GET_LINEEVENT_IOCTL _IOWR(0xB4, 0x04, struct gpioevent_request)

#endif /* _UAPI_GPIO_H_ */
