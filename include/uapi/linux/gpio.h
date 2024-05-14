/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
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

#include <linux/const.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * The maximum size of name and label arrays.
 *
 * Must be a multiple of 8 to ensure 32/64-bit alignment of structs.
 */
#define GPIO_MAX_NAME_SIZE 32

/**
 * struct gpiochip_info - Information about a certain GPIO chip
 * @name: the Linux kernel name of this GPIO chip
 * @label: a functional name for this GPIO chip, such as a product
 * number, may be empty (i.e. label[0] == '\0')
 * @lines: number of GPIO lines on this chip
 */
struct gpiochip_info {
	char name[GPIO_MAX_NAME_SIZE];
	char label[GPIO_MAX_NAME_SIZE];
	__u32 lines;
};

/*
 * Maximum number of requested lines.
 *
 * Must be no greater than 64, as bitmaps are restricted here to 64-bits
 * for simplicity, and a multiple of 2 to ensure 32/64-bit alignment of
 * structs.
 */
#define GPIO_V2_LINES_MAX 64

/*
 * The maximum number of configuration attributes associated with a line
 * request.
 */
#define GPIO_V2_LINE_NUM_ATTRS_MAX 10

/**
 * enum gpio_v2_line_flag - &struct gpio_v2_line_attribute.flags values
 * @GPIO_V2_LINE_FLAG_USED: line is not available for request
 * @GPIO_V2_LINE_FLAG_ACTIVE_LOW: line active state is physical low
 * @GPIO_V2_LINE_FLAG_INPUT: line is an input
 * @GPIO_V2_LINE_FLAG_OUTPUT: line is an output
 * @GPIO_V2_LINE_FLAG_EDGE_RISING: line detects rising (inactive to active)
 * edges
 * @GPIO_V2_LINE_FLAG_EDGE_FALLING: line detects falling (active to
 * inactive) edges
 * @GPIO_V2_LINE_FLAG_OPEN_DRAIN: line is an open drain output
 * @GPIO_V2_LINE_FLAG_OPEN_SOURCE: line is an open source output
 * @GPIO_V2_LINE_FLAG_BIAS_PULL_UP: line has pull-up bias enabled
 * @GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN: line has pull-down bias enabled
 * @GPIO_V2_LINE_FLAG_BIAS_DISABLED: line has bias disabled
 * @GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME: line events contain REALTIME timestamps
 * @GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE: line events contain timestamps from
 * hardware timestamp engine
 */
enum gpio_v2_line_flag {
	GPIO_V2_LINE_FLAG_USED			= _BITULL(0),
	GPIO_V2_LINE_FLAG_ACTIVE_LOW		= _BITULL(1),
	GPIO_V2_LINE_FLAG_INPUT			= _BITULL(2),
	GPIO_V2_LINE_FLAG_OUTPUT		= _BITULL(3),
	GPIO_V2_LINE_FLAG_EDGE_RISING		= _BITULL(4),
	GPIO_V2_LINE_FLAG_EDGE_FALLING		= _BITULL(5),
	GPIO_V2_LINE_FLAG_OPEN_DRAIN		= _BITULL(6),
	GPIO_V2_LINE_FLAG_OPEN_SOURCE		= _BITULL(7),
	GPIO_V2_LINE_FLAG_BIAS_PULL_UP		= _BITULL(8),
	GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN	= _BITULL(9),
	GPIO_V2_LINE_FLAG_BIAS_DISABLED		= _BITULL(10),
	GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME	= _BITULL(11),
	GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE	= _BITULL(12),
};

/**
 * struct gpio_v2_line_values - Values of GPIO lines
 * @bits: a bitmap containing the value of the lines, set to 1 for active
 * and 0 for inactive.
 * @mask: a bitmap identifying the lines to get or set, with each bit
 * number corresponding to the index into &struct
 * gpio_v2_line_request.offsets.
 */
struct gpio_v2_line_values {
	__aligned_u64 bits;
	__aligned_u64 mask;
};

/**
 * enum gpio_v2_line_attr_id - &struct gpio_v2_line_attribute.id values
 * identifying which field of the attribute union is in use.
 * @GPIO_V2_LINE_ATTR_ID_FLAGS: flags field is in use
 * @GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES: values field is in use
 * @GPIO_V2_LINE_ATTR_ID_DEBOUNCE: debounce_period_us field is in use
 */
enum gpio_v2_line_attr_id {
	GPIO_V2_LINE_ATTR_ID_FLAGS		= 1,
	GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES	= 2,
	GPIO_V2_LINE_ATTR_ID_DEBOUNCE		= 3,
};

/**
 * struct gpio_v2_line_attribute - a configurable attribute of a line
 * @id: attribute identifier with value from &enum gpio_v2_line_attr_id
 * @padding: reserved for future use and must be zero filled
 * @flags: if id is %GPIO_V2_LINE_ATTR_ID_FLAGS, the flags for the GPIO
 * line, with values from &enum gpio_v2_line_flag, such as
 * %GPIO_V2_LINE_FLAG_ACTIVE_LOW, %GPIO_V2_LINE_FLAG_OUTPUT etc, added
 * together.  This overrides the default flags contained in the &struct
 * gpio_v2_line_config for the associated line.
 * @values: if id is %GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES, a bitmap
 * containing the values to which the lines will be set, with each bit
 * number corresponding to the index into &struct
 * gpio_v2_line_request.offsets.
 * @debounce_period_us: if id is %GPIO_V2_LINE_ATTR_ID_DEBOUNCE, the
 * desired debounce period, in microseconds
 */
struct gpio_v2_line_attribute {
	__u32 id;
	__u32 padding;
	union {
		__aligned_u64 flags;
		__aligned_u64 values;
		__u32 debounce_period_us;
	};
};

/**
 * struct gpio_v2_line_config_attribute - a configuration attribute
 * associated with one or more of the requested lines.
 * @attr: the configurable attribute
 * @mask: a bitmap identifying the lines to which the attribute applies,
 * with each bit number corresponding to the index into &struct
 * gpio_v2_line_request.offsets.
 */
struct gpio_v2_line_config_attribute {
	struct gpio_v2_line_attribute attr;
	__aligned_u64 mask;
};

/**
 * struct gpio_v2_line_config - Configuration for GPIO lines
 * @flags: flags for the GPIO lines, with values from &enum
 * gpio_v2_line_flag, such as %GPIO_V2_LINE_FLAG_ACTIVE_LOW,
 * %GPIO_V2_LINE_FLAG_OUTPUT etc, added together.  This is the default for
 * all requested lines but may be overridden for particular lines using
 * @attrs.
 * @num_attrs: the number of attributes in @attrs
 * @padding: reserved for future use and must be zero filled
 * @attrs: the configuration attributes associated with the requested
 * lines.  Any attribute should only be associated with a particular line
 * once.  If an attribute is associated with a line multiple times then the
 * first occurrence (i.e. lowest index) has precedence.
 */
struct gpio_v2_line_config {
	__aligned_u64 flags;
	__u32 num_attrs;
	/* Pad to fill implicit padding and reserve space for future use. */
	__u32 padding[5];
	struct gpio_v2_line_config_attribute attrs[GPIO_V2_LINE_NUM_ATTRS_MAX];
};

/**
 * struct gpio_v2_line_request - Information about a request for GPIO lines
 * @offsets: an array of desired lines, specified by offset index for the
 * associated GPIO chip
 * @consumer: a desired consumer label for the selected GPIO lines such as
 * "my-bitbanged-relay"
 * @config: requested configuration for the lines.
 * @num_lines: number of lines requested in this request, i.e. the number
 * of valid fields in the %GPIO_V2_LINES_MAX sized arrays, set to 1 to
 * request a single line
 * @event_buffer_size: a suggested minimum number of line events that the
 * kernel should buffer.  This is only relevant if edge detection is
 * enabled in the configuration. Note that this is only a suggested value
 * and the kernel may allocate a larger buffer or cap the size of the
 * buffer. If this field is zero then the buffer size defaults to a minimum
 * of @num_lines * 16.
 * @padding: reserved for future use and must be zero filled
 * @fd: if successful this field will contain a valid anonymous file handle
 * after a %GPIO_GET_LINE_IOCTL operation, zero or negative value means
 * error
 */
struct gpio_v2_line_request {
	__u32 offsets[GPIO_V2_LINES_MAX];
	char consumer[GPIO_MAX_NAME_SIZE];
	struct gpio_v2_line_config config;
	__u32 num_lines;
	__u32 event_buffer_size;
	/* Pad to fill implicit padding and reserve space for future use. */
	__u32 padding[5];
	__s32 fd;
};

/**
 * struct gpio_v2_line_info - Information about a certain GPIO line
 * @name: the name of this GPIO line, such as the output pin of the line on
 * the chip, a rail or a pin header name on a board, as specified by the
 * GPIO chip, may be empty (i.e. name[0] == '\0')
 * @consumer: a functional name for the consumer of this GPIO line as set
 * by whatever is using it, will be empty if there is no current user but
 * may also be empty if the consumer doesn't set this up
 * @offset: the local offset on this GPIO chip, fill this in when
 * requesting the line information from the kernel
 * @num_attrs: the number of attributes in @attrs
 * @flags: flags for this GPIO line, with values from &enum
 * gpio_v2_line_flag, such as %GPIO_V2_LINE_FLAG_ACTIVE_LOW,
 * %GPIO_V2_LINE_FLAG_OUTPUT etc, added together.
 * @attrs: the configuration attributes associated with the line
 * @padding: reserved for future use
 */
struct gpio_v2_line_info {
	char name[GPIO_MAX_NAME_SIZE];
	char consumer[GPIO_MAX_NAME_SIZE];
	__u32 offset;
	__u32 num_attrs;
	__aligned_u64 flags;
	struct gpio_v2_line_attribute attrs[GPIO_V2_LINE_NUM_ATTRS_MAX];
	/* Space reserved for future use. */
	__u32 padding[4];
};

/**
 * enum gpio_v2_line_changed_type - &struct gpio_v2_line_changed.event_type
 * values
 * @GPIO_V2_LINE_CHANGED_REQUESTED: line has been requested
 * @GPIO_V2_LINE_CHANGED_RELEASED: line has been released
 * @GPIO_V2_LINE_CHANGED_CONFIG: line has been reconfigured
 */
enum gpio_v2_line_changed_type {
	GPIO_V2_LINE_CHANGED_REQUESTED	= 1,
	GPIO_V2_LINE_CHANGED_RELEASED	= 2,
	GPIO_V2_LINE_CHANGED_CONFIG	= 3,
};

/**
 * struct gpio_v2_line_info_changed - Information about a change in status
 * of a GPIO line
 * @info: updated line information
 * @timestamp_ns: estimate of time of status change occurrence, in nanoseconds
 * @event_type: the type of change with a value from &enum
 * gpio_v2_line_changed_type
 * @padding: reserved for future use
 */
struct gpio_v2_line_info_changed {
	struct gpio_v2_line_info info;
	__aligned_u64 timestamp_ns;
	__u32 event_type;
	/* Pad struct to 64-bit boundary and reserve space for future use. */
	__u32 padding[5];
};

/**
 * enum gpio_v2_line_event_id - &struct gpio_v2_line_event.id values
 * @GPIO_V2_LINE_EVENT_RISING_EDGE: event triggered by a rising edge
 * @GPIO_V2_LINE_EVENT_FALLING_EDGE: event triggered by a falling edge
 */
enum gpio_v2_line_event_id {
	GPIO_V2_LINE_EVENT_RISING_EDGE	= 1,
	GPIO_V2_LINE_EVENT_FALLING_EDGE	= 2,
};

/**
 * struct gpio_v2_line_event - The actual event being pushed to userspace
 * @timestamp_ns: best estimate of time of event occurrence, in nanoseconds.
 * @id: event identifier with value from &enum gpio_v2_line_event_id
 * @offset: the offset of the line that triggered the event
 * @seqno: the sequence number for this event in the sequence of events for
 * all the lines in this line request
 * @line_seqno: the sequence number for this event in the sequence of
 * events on this particular line
 * @padding: reserved for future use
 *
 * By default the @timestamp_ns is read from %CLOCK_MONOTONIC and is
 * intended to allow the accurate measurement of the time between events.
 * It does not provide the wall-clock time.
 *
 * If the %GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME flag is set then the
 * @timestamp_ns is read from %CLOCK_REALTIME.
 */
struct gpio_v2_line_event {
	__aligned_u64 timestamp_ns;
	__u32 id;
	__u32 offset;
	__u32 seqno;
	__u32 line_seqno;
	/* Space reserved for future use. */
	__u32 padding[6];
};

/*
 * ABI v1
 *
 * This version of the ABI is deprecated.
 * Use the latest version of the ABI, defined above, instead.
 */

/* Informational flags */
#define GPIOLINE_FLAG_KERNEL		(1UL << 0) /* Line used by the kernel */
#define GPIOLINE_FLAG_IS_OUT		(1UL << 1)
#define GPIOLINE_FLAG_ACTIVE_LOW	(1UL << 2)
#define GPIOLINE_FLAG_OPEN_DRAIN	(1UL << 3)
#define GPIOLINE_FLAG_OPEN_SOURCE	(1UL << 4)
#define GPIOLINE_FLAG_BIAS_PULL_UP	(1UL << 5)
#define GPIOLINE_FLAG_BIAS_PULL_DOWN	(1UL << 6)
#define GPIOLINE_FLAG_BIAS_DISABLE	(1UL << 7)

/**
 * struct gpioline_info - Information about a certain GPIO line
 * @line_offset: the local offset on this GPIO device, fill this in when
 * requesting the line information from the kernel
 * @flags: various flags for this line
 * @name: the name of this GPIO line, such as the output pin of the line on the
 * chip, a rail or a pin header name on a board, as specified by the gpio
 * chip, may be empty (i.e. name[0] == '\0')
 * @consumer: a functional name for the consumer of this GPIO line as set by
 * whatever is using it, will be empty if there is no current user but may
 * also be empty if the consumer doesn't set this up
 *
 * Note: This struct is part of ABI v1 and is deprecated.
 * Use &struct gpio_v2_line_info instead.
 */
struct gpioline_info {
	__u32 line_offset;
	__u32 flags;
	char name[GPIO_MAX_NAME_SIZE];
	char consumer[GPIO_MAX_NAME_SIZE];
};

/* Maximum number of requested handles */
#define GPIOHANDLES_MAX 64

/* Possible line status change events */
enum {
	GPIOLINE_CHANGED_REQUESTED = 1,
	GPIOLINE_CHANGED_RELEASED,
	GPIOLINE_CHANGED_CONFIG,
};

/**
 * struct gpioline_info_changed - Information about a change in status
 * of a GPIO line
 * @info: updated line information
 * @timestamp: estimate of time of status change occurrence, in nanoseconds
 * @event_type: one of %GPIOLINE_CHANGED_REQUESTED,
 * %GPIOLINE_CHANGED_RELEASED and %GPIOLINE_CHANGED_CONFIG
 * @padding: reserved for future use
 *
 * The &struct gpioline_info embedded here has 32-bit alignment on its own,
 * but it works fine with 64-bit alignment too. With its 72 byte size, we can
 * guarantee there are no implicit holes between it and subsequent members.
 * The 20-byte padding at the end makes sure we don't add any implicit padding
 * at the end of the structure on 64-bit architectures.
 *
 * Note: This struct is part of ABI v1 and is deprecated.
 * Use &struct gpio_v2_line_info_changed instead.
 */
struct gpioline_info_changed {
	struct gpioline_info info;
	__u64 timestamp;
	__u32 event_type;
	__u32 padding[5]; /* for future use */
};

/* Linerequest flags */
#define GPIOHANDLE_REQUEST_INPUT	(1UL << 0)
#define GPIOHANDLE_REQUEST_OUTPUT	(1UL << 1)
#define GPIOHANDLE_REQUEST_ACTIVE_LOW	(1UL << 2)
#define GPIOHANDLE_REQUEST_OPEN_DRAIN	(1UL << 3)
#define GPIOHANDLE_REQUEST_OPEN_SOURCE	(1UL << 4)
#define GPIOHANDLE_REQUEST_BIAS_PULL_UP	(1UL << 5)
#define GPIOHANDLE_REQUEST_BIAS_PULL_DOWN	(1UL << 6)
#define GPIOHANDLE_REQUEST_BIAS_DISABLE	(1UL << 7)

/**
 * struct gpiohandle_request - Information about a GPIO handle request
 * @lineoffsets: an array of desired lines, specified by offset index for the
 * associated GPIO device
 * @flags: desired flags for the desired GPIO lines, such as
 * %GPIOHANDLE_REQUEST_OUTPUT, %GPIOHANDLE_REQUEST_ACTIVE_LOW etc, added
 * together. Note that even if multiple lines are requested, the same flags
 * must be applicable to all of them, if you want lines with individual
 * flags set, request them one by one. It is possible to select
 * a batch of input or output lines, but they must all have the same
 * characteristics, i.e. all inputs or all outputs, all active low etc
 * @default_values: if the %GPIOHANDLE_REQUEST_OUTPUT is set for a requested
 * line, this specifies the default output value, should be 0 (low) or
 * 1 (high), anything else than 0 or 1 will be interpreted as 1 (high)
 * @consumer_label: a desired consumer label for the selected GPIO line(s)
 * such as "my-bitbanged-relay"
 * @lines: number of lines requested in this request, i.e. the number of
 * valid fields in the above arrays, set to 1 to request a single line
 * @fd: if successful this field will contain a valid anonymous file handle
 * after a %GPIO_GET_LINEHANDLE_IOCTL operation, zero or negative value
 * means error
 *
 * Note: This struct is part of ABI v1 and is deprecated.
 * Use &struct gpio_v2_line_request instead.
 */
struct gpiohandle_request {
	__u32 lineoffsets[GPIOHANDLES_MAX];
	__u32 flags;
	__u8 default_values[GPIOHANDLES_MAX];
	char consumer_label[GPIO_MAX_NAME_SIZE];
	__u32 lines;
	int fd;
};

/**
 * struct gpiohandle_config - Configuration for a GPIO handle request
 * @flags: updated flags for the requested GPIO lines, such as
 * %GPIOHANDLE_REQUEST_OUTPUT, %GPIOHANDLE_REQUEST_ACTIVE_LOW etc, added
 * together
 * @default_values: if the %GPIOHANDLE_REQUEST_OUTPUT is set in flags,
 * this specifies the default output value, should be 0 (low) or
 * 1 (high), anything else than 0 or 1 will be interpreted as 1 (high)
 * @padding: reserved for future use and should be zero filled
 *
 * Note: This struct is part of ABI v1 and is deprecated.
 * Use &struct gpio_v2_line_config instead.
 */
struct gpiohandle_config {
	__u32 flags;
	__u8 default_values[GPIOHANDLES_MAX];
	__u32 padding[4]; /* padding for future use */
};

/**
 * struct gpiohandle_data - Information of values on a GPIO handle
 * @values: when getting the state of lines this contains the current
 * state of a line, when setting the state of lines these should contain
 * the desired target state
 *
 * Note: This struct is part of ABI v1 and is deprecated.
 * Use &struct gpio_v2_line_values instead.
 */
struct gpiohandle_data {
	__u8 values[GPIOHANDLES_MAX];
};

/* Eventrequest flags */
#define GPIOEVENT_REQUEST_RISING_EDGE	(1UL << 0)
#define GPIOEVENT_REQUEST_FALLING_EDGE	(1UL << 1)
#define GPIOEVENT_REQUEST_BOTH_EDGES	((1UL << 0) | (1UL << 1))

/**
 * struct gpioevent_request - Information about a GPIO event request
 * @lineoffset: the desired line to subscribe to events from, specified by
 * offset index for the associated GPIO device
 * @handleflags: desired handle flags for the desired GPIO line, such as
 * %GPIOHANDLE_REQUEST_ACTIVE_LOW or %GPIOHANDLE_REQUEST_OPEN_DRAIN
 * @eventflags: desired flags for the desired GPIO event line, such as
 * %GPIOEVENT_REQUEST_RISING_EDGE or %GPIOEVENT_REQUEST_FALLING_EDGE
 * @consumer_label: a desired consumer label for the selected GPIO line(s)
 * such as "my-listener"
 * @fd: if successful this field will contain a valid anonymous file handle
 * after a %GPIO_GET_LINEEVENT_IOCTL operation, zero or negative value
 * means error
 *
 * Note: This struct is part of ABI v1 and is deprecated.
 * Use &struct gpio_v2_line_request instead.
 */
struct gpioevent_request {
	__u32 lineoffset;
	__u32 handleflags;
	__u32 eventflags;
	char consumer_label[GPIO_MAX_NAME_SIZE];
	int fd;
};

/*
 * GPIO event types
 */
#define GPIOEVENT_EVENT_RISING_EDGE 0x01
#define GPIOEVENT_EVENT_FALLING_EDGE 0x02

/**
 * struct gpioevent_data - The actual event being pushed to userspace
 * @timestamp: best estimate of time of event occurrence, in nanoseconds
 * @id: event identifier
 *
 * Note: This struct is part of ABI v1 and is deprecated.
 * Use &struct gpio_v2_line_event instead.
 */
struct gpioevent_data {
	__u64 timestamp;
	__u32 id;
};

/*
 * v1 and v2 ioctl()s
 */
#define GPIO_GET_CHIPINFO_IOCTL _IOR(0xB4, 0x01, struct gpiochip_info)
#define GPIO_GET_LINEINFO_UNWATCH_IOCTL _IOWR(0xB4, 0x0C, __u32)

/*
 * v2 ioctl()s
 */
#define GPIO_V2_GET_LINEINFO_IOCTL _IOWR(0xB4, 0x05, struct gpio_v2_line_info)
#define GPIO_V2_GET_LINEINFO_WATCH_IOCTL _IOWR(0xB4, 0x06, struct gpio_v2_line_info)
#define GPIO_V2_GET_LINE_IOCTL _IOWR(0xB4, 0x07, struct gpio_v2_line_request)
#define GPIO_V2_LINE_SET_CONFIG_IOCTL _IOWR(0xB4, 0x0D, struct gpio_v2_line_config)
#define GPIO_V2_LINE_GET_VALUES_IOCTL _IOWR(0xB4, 0x0E, struct gpio_v2_line_values)
#define GPIO_V2_LINE_SET_VALUES_IOCTL _IOWR(0xB4, 0x0F, struct gpio_v2_line_values)

/*
 * v1 ioctl()s
 *
 * These ioctl()s are deprecated.  Use the v2 equivalent instead.
 */
#define GPIO_GET_LINEINFO_IOCTL _IOWR(0xB4, 0x02, struct gpioline_info)
#define GPIO_GET_LINEHANDLE_IOCTL _IOWR(0xB4, 0x03, struct gpiohandle_request)
#define GPIO_GET_LINEEVENT_IOCTL _IOWR(0xB4, 0x04, struct gpioevent_request)
#define GPIOHANDLE_GET_LINE_VALUES_IOCTL _IOWR(0xB4, 0x08, struct gpiohandle_data)
#define GPIOHANDLE_SET_LINE_VALUES_IOCTL _IOWR(0xB4, 0x09, struct gpiohandle_data)
#define GPIOHANDLE_SET_CONFIG_IOCTL _IOWR(0xB4, 0x0A, struct gpiohandle_config)
#define GPIO_GET_LINEINFO_WATCH_IOCTL _IOWR(0xB4, 0x0B, struct gpioline_info)

#endif /* _UAPI_GPIO_H_ */
