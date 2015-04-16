/* The industrial I/O - event passing to userspace
 *
 * Copyright (c) 2008-2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef _IIO_EVENTS_H_
#define _IIO_EVENTS_H_

#include <linux/iio/types.h>
#include <uapi/linux/iio/events.h>

/**
 * IIO_EVENT_CODE() - create event identifier
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @diff:	Whether the event is for an differential channel or not.
 * @modifier:	Modifier for the channel. Should be one of enum iio_modifier.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 * @type:	Type of the event. Should be one of enum iio_event_type.
 * @chan:	Channel number for non-differential channels.
 * @chan1:	First channel number for differential channels.
 * @chan2:	Second channel number for differential channels.
 */

#define IIO_EVENT_CODE(chan_type, diff, modifier, direction,		\
		       type, chan, chan1, chan2)			\
	(((u64)type << 56) | ((u64)diff << 55) |			\
	 ((u64)direction << 48) | ((u64)modifier << 40) |		\
	 ((u64)chan_type << 32) | (((u16)chan2) << 16) | ((u16)chan1) | \
	 ((u16)chan))


/**
 * IIO_MOD_EVENT_CODE() - create event identifier for modified channels
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @number:	Channel number.
 * @modifier:	Modifier for the channel. Should be one of enum iio_modifier.
 * @type:	Type of the event. Should be one of enum iio_event_type.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 */

#define IIO_MOD_EVENT_CODE(chan_type, number, modifier,		\
			   type, direction)				\
	IIO_EVENT_CODE(chan_type, 0, modifier, direction, type, number, 0, 0)

/**
 * IIO_UNMOD_EVENT_CODE() - create event identifier for unmodified channels
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @number:	Channel number.
 * @type:	Type of the event. Should be one of enum iio_event_type.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 */

#define IIO_UNMOD_EVENT_CODE(chan_type, number, type, direction)	\
	IIO_EVENT_CODE(chan_type, 0, 0, direction, type, number, 0, 0)

#endif
