/* SPDX-License-Identifier: GPL-2.0-only */
/* The industrial I/O - event passing to userspace
 *
 * Copyright (c) 2008-2011 Jonathan Cameron
 */
#ifndef _IIO_EVENTS_H_
#define _IIO_EVENTS_H_

#include <linux/iio/types.h>
#include <uapi/linux/iio/events.h>

/**
 * _IIO_EVENT_CODE() - create event identifier
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @diff:	Whether the event is for an differential channel or not.
 * @modifier:	Modifier for the channel. Should be one of enum iio_modifier.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 * @type:	Type of the event. Should be one of enum iio_event_type.
 * @chan:	Channel number for non-differential channels.
 * @chan1:	First channel number for differential channels.
 * @chan2:	Second channel number for differential channels.
 *
 * Drivers should use the specialized macros below instead of using this one
 * directly.
 */

#define _IIO_EVENT_CODE(chan_type, diff, modifier, direction,		\
			type, chan, chan1, chan2)			\
	(((u64)type << 56) | ((u64)diff << 55) |			\
	 ((u64)direction << 48) | ((u64)modifier << 40) |		\
	 ((u64)chan_type << 32) | (((u16)chan2) << 16) | ((u16)chan1) | \
	 ((u16)chan))


/**
 * IIO_MOD_EVENT_CODE() - create event identifier for modified (non
 * differential) channels
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @number:	Channel number.
 * @modifier:	Modifier for the channel. Should be one of enum iio_modifier.
 * @type:	Type of the event. Should be one of enum iio_event_type.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 */

#define IIO_MOD_EVENT_CODE(chan_type, number, modifier,		\
			   type, direction)				\
	_IIO_EVENT_CODE(chan_type, 0, modifier, direction, type, number, 0, 0)

/**
 * IIO_UNMOD_EVENT_CODE() - create event identifier for unmodified (non
 * differential) channels
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @number:	Channel number.
 * @type:	Type of the event. Should be one of enum iio_event_type.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 */

#define IIO_UNMOD_EVENT_CODE(chan_type, number, type, direction)	\
	_IIO_EVENT_CODE(chan_type, 0, 0, direction, type, number, 0, 0)

/**
 * IIO_DIFF_EVENT_CODE() - create event identifier for differential channels
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @chan1:	First channel number for differential channels.
 * @chan2:	Second channel number for differential channels.
 * @type:	Type of the event. Should be one of enum iio_event_type.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 */

#define IIO_DIFF_EVENT_CODE(chan_type, chan1, chan2, type, direction)	\
	_IIO_EVENT_CODE(chan_type, 1, 0, direction, type, 0, chan1, chan2)

#endif
