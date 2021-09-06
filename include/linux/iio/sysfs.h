/* SPDX-License-Identifier: GPL-2.0-only */
/* The industrial I/O core
 *
 *Copyright (c) 2008 Jonathan Cameron
 *
 * General attributes
 */

#ifndef _INDUSTRIAL_IO_SYSFS_H_
#define _INDUSTRIAL_IO_SYSFS_H_

struct iio_buffer;
struct iio_chan_spec;

/**
 * struct iio_dev_attr - iio specific device attribute
 * @dev_attr:	underlying device attribute
 * @address:	associated register address
 * @l:		list head for maintaining list of dynamically created attrs
 * @c:		specification for the underlying channel
 * @buffer:	the IIO buffer to which this attribute belongs to (if any)
 */
struct iio_dev_attr {
	struct device_attribute dev_attr;
	u64 address;
	struct list_head l;
	struct iio_chan_spec const *c;
	struct iio_buffer *buffer;
};

#define to_iio_dev_attr(_dev_attr)				\
	container_of(_dev_attr, struct iio_dev_attr, dev_attr)

ssize_t iio_read_const_attr(struct device *dev,
			    struct device_attribute *attr,
			    char *len);

/**
 * struct iio_const_attr - constant device specific attribute
 *                         often used for things like available modes
 * @string:	attribute string
 * @dev_attr:	underlying device attribute
 */
struct iio_const_attr {
	const char *string;
	struct device_attribute dev_attr;
};

#define to_iio_const_attr(_dev_attr) \
	container_of(_dev_attr, struct iio_const_attr, dev_attr)

/* Some attributes will be hard coded (device dependent) and not require an
   address, in these cases pass a negative */
#define IIO_ATTR(_name, _mode, _show, _store, _addr)		\
	{ .dev_attr = __ATTR(_name, _mode, _show, _store),	\
	  .address = _addr }

#define IIO_ATTR_RO(_name, _addr)       \
	{ .dev_attr = __ATTR_RO(_name), \
	  .address = _addr }

#define IIO_ATTR_WO(_name, _addr)       \
	{ .dev_attr = __ATTR_WO(_name), \
	  .address = _addr }

#define IIO_ATTR_RW(_name, _addr)       \
	{ .dev_attr = __ATTR_RW(_name), \
	  .address = _addr }

#define IIO_DEVICE_ATTR(_name, _mode, _show, _store, _addr)	\
	struct iio_dev_attr iio_dev_attr_##_name		\
	= IIO_ATTR(_name, _mode, _show, _store, _addr)

#define IIO_DEVICE_ATTR_RO(_name, _addr)                       \
	struct iio_dev_attr iio_dev_attr_##_name                \
	= IIO_ATTR_RO(_name, _addr)

#define IIO_DEVICE_ATTR_WO(_name, _addr)                       \
	struct iio_dev_attr iio_dev_attr_##_name                \
	= IIO_ATTR_WO(_name, _addr)

#define IIO_DEVICE_ATTR_RW(_name, _addr)                                   \
	struct iio_dev_attr iio_dev_attr_##_name                            \
	= IIO_ATTR_RW(_name, _addr)

#define IIO_DEVICE_ATTR_NAMED(_vname, _name, _mode, _show, _store, _addr) \
	struct iio_dev_attr iio_dev_attr_##_vname			\
	= IIO_ATTR(_name, _mode, _show, _store, _addr)

#define IIO_CONST_ATTR(_name, _string)					\
	struct iio_const_attr iio_const_attr_##_name			\
	= { .string = _string,						\
	    .dev_attr = __ATTR(_name, S_IRUGO, iio_read_const_attr, NULL)}

#define IIO_CONST_ATTR_NAMED(_vname, _name, _string)			\
	struct iio_const_attr iio_const_attr_##_vname			\
	= { .string = _string,						\
	    .dev_attr = __ATTR(_name, S_IRUGO, iio_read_const_attr, NULL)}

/* Generic attributes of onetype or another */

/**
 * IIO_DEV_ATTR_SAMP_FREQ - sets any internal clock frequency
 * @_mode: sysfs file mode/permissions
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 **/
#define IIO_DEV_ATTR_SAMP_FREQ(_mode, _show, _store)			\
	IIO_DEVICE_ATTR(sampling_frequency, _mode, _show, _store, 0)

/**
 * IIO_DEV_ATTR_SAMP_FREQ_AVAIL - list available sampling frequencies
 * @_show: output method for the attribute
 *
 * May be mode dependent on some devices
 **/
#define IIO_DEV_ATTR_SAMP_FREQ_AVAIL(_show)				\
	IIO_DEVICE_ATTR(sampling_frequency_available, S_IRUGO, _show, NULL, 0)
/**
 * IIO_CONST_ATTR_SAMP_FREQ_AVAIL - list available sampling frequencies
 * @_string: frequency string for the attribute
 *
 * Constant version
 **/
#define IIO_CONST_ATTR_SAMP_FREQ_AVAIL(_string)			\
	IIO_CONST_ATTR(sampling_frequency_available, _string)

/**
 * IIO_DEV_ATTR_INT_TIME_AVAIL - list available integration times
 * @_show: output method for the attribute
 **/
#define IIO_DEV_ATTR_INT_TIME_AVAIL(_show)		\
	IIO_DEVICE_ATTR(integration_time_available, S_IRUGO, _show, NULL, 0)
/**
 * IIO_CONST_ATTR_INT_TIME_AVAIL - list available integration times
 * @_string: frequency string for the attribute
 *
 * Constant version
 **/
#define IIO_CONST_ATTR_INT_TIME_AVAIL(_string)		\
	IIO_CONST_ATTR(integration_time_available, _string)

#define IIO_DEV_ATTR_TEMP_RAW(_show)			\
	IIO_DEVICE_ATTR(in_temp_raw, S_IRUGO, _show, NULL, 0)

#define IIO_CONST_ATTR_TEMP_OFFSET(_string)		\
	IIO_CONST_ATTR(in_temp_offset, _string)

#define IIO_CONST_ATTR_TEMP_SCALE(_string)		\
	IIO_CONST_ATTR(in_temp_scale, _string)

#endif /* _INDUSTRIAL_IO_SYSFS_H_ */
