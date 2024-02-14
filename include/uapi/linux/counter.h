/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace ABI for Counter character devices
 * Copyright (C) 2020 William Breathitt Gray
 */
#ifndef _UAPI_COUNTER_H_
#define _UAPI_COUNTER_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/* Component type definitions */
enum counter_component_type {
	COUNTER_COMPONENT_NONE,
	COUNTER_COMPONENT_SIGNAL,
	COUNTER_COMPONENT_COUNT,
	COUNTER_COMPONENT_FUNCTION,
	COUNTER_COMPONENT_SYNAPSE_ACTION,
	COUNTER_COMPONENT_EXTENSION,
};

/* Component scope definitions */
enum counter_scope {
	COUNTER_SCOPE_DEVICE,
	COUNTER_SCOPE_SIGNAL,
	COUNTER_SCOPE_COUNT,
};

/**
 * struct counter_component - Counter component identification
 * @type: component type (one of enum counter_component_type)
 * @scope: component scope (one of enum counter_scope)
 * @parent: parent ID (matching the ID suffix of the respective parent sysfs
 *          path as described by the ABI documentation file
 *          Documentation/ABI/testing/sysfs-bus-counter)
 * @id: component ID (matching the ID provided by the respective *_component_id
 *      sysfs attribute of the desired component)
 *
 * For example, if the Count 2 ceiling extension of Counter device 4 is desired,
 * set type equal to COUNTER_COMPONENT_EXTENSION, scope equal to
 * COUNTER_COUNT_SCOPE, parent equal to 2, and id equal to the value provided by
 * the respective /sys/bus/counter/devices/counter4/count2/ceiling_component_id
 * sysfs attribute.
 */
struct counter_component {
	__u8 type;
	__u8 scope;
	__u8 parent;
	__u8 id;
};

/* Event type definitions */
enum counter_event_type {
	/* Count value increased past ceiling */
	COUNTER_EVENT_OVERFLOW,
	/* Count value decreased past floor */
	COUNTER_EVENT_UNDERFLOW,
	/* Count value increased past ceiling, or decreased past floor */
	COUNTER_EVENT_OVERFLOW_UNDERFLOW,
	/* Count value reached threshold */
	COUNTER_EVENT_THRESHOLD,
	/* Index signal detected */
	COUNTER_EVENT_INDEX,
	/* State of counter is changed */
	COUNTER_EVENT_CHANGE_OF_STATE,
	/* Count value captured */
	COUNTER_EVENT_CAPTURE,
};

/**
 * struct counter_watch - Counter component watch configuration
 * @component: component to watch when event triggers
 * @event: event that triggers (one of enum counter_event_type)
 * @channel: event channel (typically 0 unless the device supports concurrent
 *	     events of the same type)
 */
struct counter_watch {
	struct counter_component component;
	__u8 event;
	__u8 channel;
};

/*
 * Queues a Counter watch for the specified event.
 *
 * The queued watches will not be applied until COUNTER_ENABLE_EVENTS_IOCTL is
 * called.
 */
#define COUNTER_ADD_WATCH_IOCTL _IOW(0x3E, 0x00, struct counter_watch)
/*
 * Enables monitoring the events specified by the Counter watches that were
 * queued by COUNTER_ADD_WATCH_IOCTL.
 *
 * If events are already enabled, the new set of watches replaces the old one.
 * Calling this ioctl also has the effect of clearing the queue of watches added
 * by COUNTER_ADD_WATCH_IOCTL.
 */
#define COUNTER_ENABLE_EVENTS_IOCTL _IO(0x3E, 0x01)
/*
 * Stops monitoring the previously enabled events.
 */
#define COUNTER_DISABLE_EVENTS_IOCTL _IO(0x3E, 0x02)

/**
 * struct counter_event - Counter event data
 * @timestamp: best estimate of time of event occurrence, in nanoseconds
 * @value: component value
 * @watch: component watch configuration
 * @status: return status (system error number)
 */
struct counter_event {
	__aligned_u64 timestamp;
	__aligned_u64 value;
	struct counter_watch watch;
	__u8 status;
};

/* Count direction values */
enum counter_count_direction {
	COUNTER_COUNT_DIRECTION_FORWARD,
	COUNTER_COUNT_DIRECTION_BACKWARD,
};

/* Count mode values */
enum counter_count_mode {
	COUNTER_COUNT_MODE_NORMAL,
	COUNTER_COUNT_MODE_RANGE_LIMIT,
	COUNTER_COUNT_MODE_NON_RECYCLE,
	COUNTER_COUNT_MODE_MODULO_N,
};

/* Count function values */
enum counter_function {
	COUNTER_FUNCTION_INCREASE,
	COUNTER_FUNCTION_DECREASE,
	COUNTER_FUNCTION_PULSE_DIRECTION,
	COUNTER_FUNCTION_QUADRATURE_X1_A,
	COUNTER_FUNCTION_QUADRATURE_X1_B,
	COUNTER_FUNCTION_QUADRATURE_X2_A,
	COUNTER_FUNCTION_QUADRATURE_X2_B,
	COUNTER_FUNCTION_QUADRATURE_X4,
};

/* Signal values */
enum counter_signal_level {
	COUNTER_SIGNAL_LEVEL_LOW,
	COUNTER_SIGNAL_LEVEL_HIGH,
};

/* Action mode values */
enum counter_synapse_action {
	COUNTER_SYNAPSE_ACTION_NONE,
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
};

/* Signal polarity values */
enum counter_signal_polarity {
	COUNTER_SIGNAL_POLARITY_POSITIVE,
	COUNTER_SIGNAL_POLARITY_NEGATIVE,
};

#endif /* _UAPI_COUNTER_H_ */
