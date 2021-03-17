/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _INDUSTRIAL_IO_OPAQUE_H_
#define _INDUSTRIAL_IO_OPAQUE_H_

/**
 * struct iio_dev_opaque - industrial I/O device opaque information
 * @indio_dev:			public industrial I/O device information
 * @event_interface:		event chrdevs associated with interrupt lines
 * @buffer_list:		list of all buffers currently attached
 * @channel_attr_list:		keep track of automatically created channel
 *				attributes
 * @chan_attr_group:		group for all attrs in base directory
 * @debugfs_dentry:		device specific debugfs dentry
 * @cached_reg_addr:		cached register address for debugfs reads
 * @read_buf:			read buffer to be used for the initial reg read
 * @read_buf_len:		data length in @read_buf
 */
struct iio_dev_opaque {
	struct iio_dev			indio_dev;
	struct iio_event_interface	*event_interface;
	struct list_head		buffer_list;
	struct list_head		channel_attr_list;
	struct attribute_group		chan_attr_group;
#if defined(CONFIG_DEBUG_FS)
	struct dentry			*debugfs_dentry;
	unsigned			cached_reg_addr;
	char				read_buf[20];
	unsigned int			read_buf_len;
#endif
};

#define to_iio_dev_opaque(indio_dev)		\
	container_of(indio_dev, struct iio_dev_opaque, indio_dev)

#endif
