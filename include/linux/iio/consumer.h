/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Industrial I/O in kernel consumer interface
 *
 * Copyright (c) 2011 Jonathan Cameron
 */
#ifndef _IIO_INKERN_CONSUMER_H_
#define _IIO_INKERN_CONSUMER_H_

#include <linux/of.h>
#include <linux/types.h>
#include <linux/iio/types.h>

struct iio_dev;
struct iio_chan_spec;
struct device;
struct device_node;
struct fwnode_handle;

/**
 * struct iio_channel - everything needed for a consumer to use a channel
 * @indio_dev:		Device on which the channel exists.
 * @channel:		Full description of the channel.
 * @data:		Data about the channel used by consumer.
 */
struct iio_channel {
	struct iio_dev *indio_dev;
	const struct iio_chan_spec *channel;
	void *data;
};

/**
 * iio_channel_get() - get description of all that is needed to access channel.
 * @dev:		Pointer to consumer device. Device name must match
 *			the name of the device as provided in the iio_map
 *			with which the desired provider to consumer mapping
 *			was registered.
 * @consumer_channel:	Unique name to identify the channel on the consumer
 *			side. This typically describes the channels use within
 *			the consumer. E.g. 'battery_voltage'
 */
struct iio_channel *iio_channel_get(struct device *dev,
				    const char *consumer_channel);

/**
 * iio_channel_release() - release channels obtained via iio_channel_get
 * @chan:		The channel to be released.
 */
void iio_channel_release(struct iio_channel *chan);

/**
 * devm_iio_channel_get() - Resource managed version of iio_channel_get().
 * @dev:		Pointer to consumer device. Device name must match
 *			the name of the device as provided in the iio_map
 *			with which the desired provider to consumer mapping
 *			was registered.
 * @consumer_channel:	Unique name to identify the channel on the consumer
 *			side. This typically describes the channels use within
 *			the consumer. E.g. 'battery_voltage'
 *
 * Returns a pointer to negative errno if it is not able to get the iio channel
 * otherwise returns valid pointer for iio channel.
 *
 * The allocated iio channel is automatically released when the device is
 * unbound.
 */
struct iio_channel *devm_iio_channel_get(struct device *dev,
					 const char *consumer_channel);
/**
 * iio_channel_get_all() - get all channels associated with a client
 * @dev:		Pointer to consumer device.
 *
 * Returns an array of iio_channel structures terminated with one with
 * null iio_dev pointer.
 * This function is used by fairly generic consumers to get all the
 * channels registered as having this consumer.
 */
struct iio_channel *iio_channel_get_all(struct device *dev);

/**
 * iio_channel_release_all() - reverse iio_channel_get_all
 * @chan:		Array of channels to be released.
 */
void iio_channel_release_all(struct iio_channel *chan);

/**
 * devm_iio_channel_get_all() - Resource managed version of
 *				iio_channel_get_all().
 * @dev: Pointer to consumer device.
 *
 * Returns a pointer to negative errno if it is not able to get the iio channel
 * otherwise returns an array of iio_channel structures terminated with one with
 * null iio_dev pointer.
 *
 * This function is used by fairly generic consumers to get all the
 * channels registered as having this consumer.
 *
 * The allocated iio channels are automatically released when the device is
 * unbounded.
 */
struct iio_channel *devm_iio_channel_get_all(struct device *dev);

/**
 * fwnode_iio_channel_get_by_name() - get description of all that is needed to access channel.
 * @fwnode:		Pointer to consumer Firmware node
 * @consumer_channel:	Unique name to identify the channel on the consumer
 *			side. This typically describes the channels use within
 *			the consumer. E.g. 'battery_voltage'
 */
struct iio_channel *fwnode_iio_channel_get_by_name(struct fwnode_handle *fwnode,
						   const char *name);

/**
 * devm_fwnode_iio_channel_get_by_name() - Resource managed version of
 *					   fwnode_iio_channel_get_by_name().
 * @dev:		Pointer to consumer device.
 * @fwnode:		Pointer to consumer Firmware node
 * @consumer_channel:	Unique name to identify the channel on the consumer
 *			side. This typically describes the channels use within
 *			the consumer. E.g. 'battery_voltage'
 *
 * Returns a pointer to negative errno if it is not able to get the iio channel
 * otherwise returns valid pointer for iio channel.
 *
 * The allocated iio channel is automatically released when the device is
 * unbound.
 */
struct iio_channel *devm_fwnode_iio_channel_get_by_name(struct device *dev,
							struct fwnode_handle *fwnode,
							const char *consumer_channel);

static inline struct iio_channel
*devm_of_iio_channel_get_by_name(struct device *dev, struct device_node *np,
				 const char *consumer_channel)
{
	return devm_fwnode_iio_channel_get_by_name(dev, of_fwnode_handle(np),
						   consumer_channel);
}

struct iio_cb_buffer;
/**
 * iio_channel_get_all_cb() - register callback for triggered capture
 * @dev:		Pointer to client device.
 * @cb:			Callback function.
 * @private:		Private data passed to callback.
 *
 * NB right now we have no ability to mux data from multiple devices.
 * So if the channels requested come from different devices this will
 * fail.
 */
struct iio_cb_buffer *iio_channel_get_all_cb(struct device *dev,
					     int (*cb)(const void *data,
						       void *private),
					     void *private);
/**
 * iio_channel_cb_set_buffer_watermark() - set the buffer watermark.
 * @cb_buffer:		The callback buffer from whom we want the channel
 *			information.
 * @watermark: buffer watermark in bytes.
 *
 * This function allows to configure the buffer watermark.
 */
int iio_channel_cb_set_buffer_watermark(struct iio_cb_buffer *cb_buffer,
					size_t watermark);

/**
 * iio_channel_release_all_cb() - release and unregister the callback.
 * @cb_buffer:		The callback buffer that was allocated.
 */
void iio_channel_release_all_cb(struct iio_cb_buffer *cb_buffer);

/**
 * iio_channel_start_all_cb() - start the flow of data through callback.
 * @cb_buff:		The callback buffer we are starting.
 */
int iio_channel_start_all_cb(struct iio_cb_buffer *cb_buff);

/**
 * iio_channel_stop_all_cb() - stop the flow of data through the callback.
 * @cb_buff:		The callback buffer we are stopping.
 */
void iio_channel_stop_all_cb(struct iio_cb_buffer *cb_buff);

/**
 * iio_channel_cb_get_channels() - get access to the underlying channels.
 * @cb_buffer:		The callback buffer from whom we want the channel
 *			information.
 *
 * This function allows one to obtain information about the channels.
 * Whilst this may allow direct reading if all buffers are disabled, the
 * primary aim is to allow drivers that are consuming a channel to query
 * things like scaling of the channel.
 */
struct iio_channel
*iio_channel_cb_get_channels(const struct iio_cb_buffer *cb_buffer);

/**
 * iio_channel_cb_get_iio_dev() - get access to the underlying device.
 * @cb_buffer:		The callback buffer from whom we want the device
 *			information.
 *
 * This function allows one to obtain information about the device.
 * The primary aim is to allow drivers that are consuming a device to query
 * things like current trigger.
 */
struct iio_dev
*iio_channel_cb_get_iio_dev(const struct iio_cb_buffer *cb_buffer);

/**
 * iio_read_channel_raw() - read from a given channel
 * @chan:		The channel being queried.
 * @val:		Value read back.
 *
 * Note raw reads from iio channels are in adc counts and hence
 * scale will need to be applied if standard units required.
 */
int iio_read_channel_raw(struct iio_channel *chan,
			 int *val);

/**
 * iio_read_channel_average_raw() - read from a given channel
 * @chan:		The channel being queried.
 * @val:		Value read back.
 *
 * Note raw reads from iio channels are in adc counts and hence
 * scale will need to be applied if standard units required.
 *
 * In opposit to the normal iio_read_channel_raw this function
 * returns the average of multiple reads.
 */
int iio_read_channel_average_raw(struct iio_channel *chan, int *val);

/**
 * iio_read_channel_processed() - read processed value from a given channel
 * @chan:		The channel being queried.
 * @val:		Value read back.
 *
 * Returns an error code or 0.
 *
 * This function will read a processed value from a channel. A processed value
 * means that this value will have the correct unit and not some device internal
 * representation. If the device does not support reporting a processed value
 * the function will query the raw value and the channels scale and offset and
 * do the appropriate transformation.
 */
int iio_read_channel_processed(struct iio_channel *chan, int *val);

/**
 * iio_read_channel_processed_scale() - read and scale a processed value
 * @chan:		The channel being queried.
 * @val:		Value read back.
 * @scale:		Scale factor to apply during the conversion
 *
 * Returns an error code or 0.
 *
 * This function will read a processed value from a channel. This will work
 * like @iio_read_channel_processed() but also scale with an additional
 * scale factor while attempting to minimize any precision loss.
 */
int iio_read_channel_processed_scale(struct iio_channel *chan, int *val,
				     unsigned int scale);

/**
 * iio_write_channel_attribute() - Write values to the device attribute.
 * @chan:	The channel being queried.
 * @val:	Value being written.
 * @val2:	Value being written.val2 use depends on attribute type.
 * @attribute:	info attribute to be read.
 *
 * Returns an error code or 0.
 */
int iio_write_channel_attribute(struct iio_channel *chan, int val,
				int val2, enum iio_chan_info_enum attribute);

/**
 * iio_read_channel_attribute() - Read values from the device attribute.
 * @chan:	The channel being queried.
 * @val:	Value being written.
 * @val2:	Value being written.Val2 use depends on attribute type.
 * @attribute:	info attribute to be written.
 *
 * Returns an error code if failed. Else returns a description of what is in val
 * and val2, such as IIO_VAL_INT_PLUS_MICRO telling us we have a value of val
 * + val2/1e6
 */
int iio_read_channel_attribute(struct iio_channel *chan, int *val,
			       int *val2, enum iio_chan_info_enum attribute);

/**
 * iio_write_channel_raw() - write to a given channel
 * @chan:		The channel being queried.
 * @val:		Value being written.
 *
 * Note raw writes to iio channels are in dac counts and hence
 * scale will need to be applied if standard units required.
 */
int iio_write_channel_raw(struct iio_channel *chan, int val);

/**
 * iio_read_max_channel_raw() - read maximum available raw value from a given
 *				channel, i.e. the maximum possible value.
 * @chan:		The channel being queried.
 * @val:		Value read back.
 *
 * Note raw reads from iio channels are in adc counts and hence
 * scale will need to be applied if standard units are required.
 */
int iio_read_max_channel_raw(struct iio_channel *chan, int *val);

/**
 * iio_read_avail_channel_raw() - read available raw values from a given channel
 * @chan:		The channel being queried.
 * @vals:		Available values read back.
 * @length:		Number of entries in vals.
 *
 * Returns an error code, IIO_AVAIL_RANGE or IIO_AVAIL_LIST.
 *
 * For ranges, three vals are always returned; min, step and max.
 * For lists, all the possible values are enumerated.
 *
 * Note raw available values from iio channels are in adc counts and
 * hence scale will need to be applied if standard units are required.
 */
int iio_read_avail_channel_raw(struct iio_channel *chan,
			       const int **vals, int *length);

/**
 * iio_read_avail_channel_attribute() - read available channel attribute values
 * @chan:		The channel being queried.
 * @vals:		Available values read back.
 * @type:		Type of values read back.
 * @length:		Number of entries in vals.
 * @attribute:		info attribute to be read back.
 *
 * Returns an error code, IIO_AVAIL_RANGE or IIO_AVAIL_LIST.
 */
int iio_read_avail_channel_attribute(struct iio_channel *chan,
				     const int **vals, int *type, int *length,
				     enum iio_chan_info_enum attribute);

/**
 * iio_get_channel_type() - get the type of a channel
 * @channel:		The channel being queried.
 * @type:		The type of the channel.
 *
 * returns the enum iio_chan_type of the channel
 */
int iio_get_channel_type(struct iio_channel *channel,
			 enum iio_chan_type *type);

/**
 * iio_read_channel_offset() - read the offset value for a channel
 * @chan:		The channel being queried.
 * @val:		First part of value read back.
 * @val2:		Second part of value read back.
 *
 * Note returns a description of what is in val and val2, such
 * as IIO_VAL_INT_PLUS_MICRO telling us we have a value of val
 * + val2/1e6
 */
int iio_read_channel_offset(struct iio_channel *chan, int *val,
			   int *val2);

/**
 * iio_read_channel_scale() - read the scale value for a channel
 * @chan:		The channel being queried.
 * @val:		First part of value read back.
 * @val2:		Second part of value read back.
 *
 * Note returns a description of what is in val and val2, such
 * as IIO_VAL_INT_PLUS_MICRO telling us we have a value of val
 * + val2/1e6
 */
int iio_read_channel_scale(struct iio_channel *chan, int *val,
			   int *val2);

/**
 * iio_convert_raw_to_processed() - Converts a raw value to a processed value
 * @chan:		The channel being queried
 * @raw:		The raw IIO to convert
 * @processed:		The result of the conversion
 * @scale:		Scale factor to apply during the conversion
 *
 * Returns an error code or 0.
 *
 * This function converts a raw value to processed value for a specific channel.
 * A raw value is the device internal representation of a sample and the value
 * returned by iio_read_channel_raw, so the unit of that value is device
 * depended. A processed value on the other hand is value has a normed unit
 * according with the IIO specification.
 *
 * The scale factor allows to increase the precession of the returned value. For
 * a scale factor of 1 the function will return the result in the normal IIO
 * unit for the channel type. E.g. millivolt for voltage channels, if you want
 * nanovolts instead pass 1000000 as the scale factor.
 */
int iio_convert_raw_to_processed(struct iio_channel *chan, int raw,
	int *processed, unsigned int scale);

/**
 * iio_get_channel_ext_info_count() - get number of ext_info attributes
 *				      connected to the channel.
 * @chan:		The channel being queried
 *
 * Returns the number of ext_info attributes
 */
unsigned int iio_get_channel_ext_info_count(struct iio_channel *chan);

/**
 * iio_read_channel_ext_info() - read ext_info attribute from a given channel
 * @chan:		The channel being queried.
 * @attr:		The ext_info attribute to read.
 * @buf:		Where to store the attribute value. Assumed to hold
 *			at least PAGE_SIZE bytes.
 *
 * Returns the number of bytes written to buf (perhaps w/o zero termination;
 * it need not even be a string), or an error code.
 */
ssize_t iio_read_channel_ext_info(struct iio_channel *chan,
				  const char *attr, char *buf);

/**
 * iio_write_channel_ext_info() - write ext_info attribute from a given channel
 * @chan:		The channel being queried.
 * @attr:		The ext_info attribute to read.
 * @buf:		The new attribute value. Strings needs to be zero-
 *			terminated, but the terminator should not be included
 *			in the below len.
 * @len:		The size of the new attribute value.
 *
 * Returns the number of accepted bytes, which should be the same as len.
 * An error code can also be returned.
 */
ssize_t iio_write_channel_ext_info(struct iio_channel *chan, const char *attr,
				   const char *buf, size_t len);

#endif
