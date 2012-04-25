/*
 * Industrial I/O in kernel consumer interface
 *
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef _IIO_INKERN_CONSUMER_H_
#define _IIO_INKERN_CONSUMER_H
#include <linux/iio/types.h>

struct iio_dev;
struct iio_chan_spec;

/**
 * struct iio_channel - everything needed for a consumer to use a channel
 * @indio_dev:		Device on which the channel exists.
 * @channel:		Full description of the channel.
 */
struct iio_channel {
	struct iio_dev *indio_dev;
	const struct iio_chan_spec *channel;
};

/**
 * iio_channel_get() - get description of all that is needed to access channel.
 * @name:		Unique name of the device as provided in the iio_map
 *			with which the desired provider to consumer mapping
 *			was registered.
 * @consumer_channel:	Unique name to identify the channel on the consumer
 *			side. This typically describes the channels use within
 *			the consumer. E.g. 'battery_voltage'
 */
struct iio_channel *iio_st_channel_get(const char *name,
				       const char *consumer_channel);

/**
 * iio_st_channel_release() - release channels obtained via iio_st_channel_get
 * @chan:		The channel to be released.
 */
void iio_st_channel_release(struct iio_channel *chan);

/**
 * iio_st_channel_get_all() - get all channels associated with a client
 * @name:		name of consumer device.
 *
 * Returns an array of iio_channel structures terminated with one with
 * null iio_dev pointer.
 * This function is used by fairly generic consumers to get all the
 * channels registered as having this consumer.
 */
struct iio_channel *iio_st_channel_get_all(const char *name);

/**
 * iio_st_channel_release_all() - reverse iio_st_get_all
 * @chan:		Array of channels to be released.
 */
void iio_st_channel_release_all(struct iio_channel *chan);

/**
 * iio_st_read_channel_raw() - read from a given channel
 * @channel:		The channel being queried.
 * @val:		Value read back.
 *
 * Note raw reads from iio channels are in adc counts and hence
 * scale will need to be applied if standard units required.
 */
int iio_st_read_channel_raw(struct iio_channel *chan,
			    int *val);

/**
 * iio_st_get_channel_type() - get the type of a channel
 * @channel:		The channel being queried.
 * @type:		The type of the channel.
 *
 * returns the enum iio_chan_type of the channel
 */
int iio_st_get_channel_type(struct iio_channel *channel,
			    enum iio_chan_type *type);

/**
 * iio_st_read_channel_scale() - read the scale value for a channel
 * @channel:		The channel being queried.
 * @val:		First part of value read back.
 * @val2:		Second part of value read back.
 *
 * Note returns a description of what is in val and val2, such
 * as IIO_VAL_INT_PLUS_MICRO telling us we have a value of val
 * + val2/1e6
 */
int iio_st_read_channel_scale(struct iio_channel *chan, int *val,
			      int *val2);

#endif
