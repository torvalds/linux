/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _INDUSTRIAL_IO_OPAQUE_H_
#define _INDUSTRIAL_IO_OPAQUE_H_

/**
 * struct iio_dev_opaque - industrial I/O device opaque information
 * @indio_dev:			public industrial I/O device information
 * @id:			used to identify device internally
 * @currentmode:		operating mode currently in use, may be eventually
 *				checked by device drivers but should be considered
 *				read-only as this is a core internal bit
 * @driver_module:		used to make it harder to undercut users
 * @mlock:			lock used to prevent simultaneous device state changes
 * @mlock_key:			lockdep class for iio_dev lock
 * @info_exist_lock:		lock to prevent use during removal
 * @trig_readonly:		mark the current trigger immutable
 * @event_interface:		event chrdevs associated with interrupt lines
 * @attached_buffers:		array of buffers statically attached by the driver
 * @attached_buffers_cnt:	number of buffers in the array of statically attached buffers
 * @buffer_ioctl_handler:	ioctl() handler for this IIO device's buffer interface
 * @buffer_list:		list of all buffers currently attached
 * @channel_attr_list:		keep track of automatically created channel
 *				attributes
 * @chan_attr_group:		group for all attrs in base directory
 * @ioctl_handlers:		ioctl handlers registered with the core handler
 * @groups:			attribute groups
 * @groupcounter:		index of next attribute group
 * @legacy_scan_el_group:	attribute group for legacy scan elements attribute group
 * @legacy_buffer_group:	attribute group for legacy buffer attributes group
 * @bounce_buffer:		for devices that call iio_push_to_buffers_with_timestamp_unaligned()
 * @bounce_buffer_size:		size of currently allocate bounce buffer
 * @scan_index_timestamp:	cache of the index to the timestamp
 * @clock_id:			timestamping clock posix identifier
 * @chrdev:			associated character device
 * @flags:			file ops related flags including busy flag.
 * @debugfs_dentry:		device specific debugfs dentry
 * @cached_reg_addr:		cached register address for debugfs reads
 * @read_buf:			read buffer to be used for the initial reg read
 * @read_buf_len:		data length in @read_buf
 */
struct iio_dev_opaque {
	struct iio_dev			indio_dev;
	int				currentmode;
	int				id;
	struct module			*driver_module;
	struct mutex			mlock;
	struct lock_class_key		mlock_key;
	struct mutex			info_exist_lock;
	bool				trig_readonly;
	struct iio_event_interface	*event_interface;
	struct iio_buffer		**attached_buffers;
	unsigned int			attached_buffers_cnt;
	struct iio_ioctl_handler	*buffer_ioctl_handler;
	struct list_head		buffer_list;
	struct list_head		channel_attr_list;
	struct attribute_group		chan_attr_group;
	struct list_head		ioctl_handlers;
	const struct attribute_group	**groups;
	int				groupcounter;
	struct attribute_group		legacy_scan_el_group;
	struct attribute_group		legacy_buffer_group;
	void				*bounce_buffer;
	size_t				bounce_buffer_size;

	unsigned int			scan_index_timestamp;
	clockid_t			clock_id;
	struct cdev			chrdev;
	unsigned long			flags;

#if defined(CONFIG_DEBUG_FS)
	struct dentry			*debugfs_dentry;
	unsigned			cached_reg_addr;
	char				read_buf[20];
	unsigned int			read_buf_len;
#endif
};

#define to_iio_dev_opaque(_indio_dev)		\
	container_of((_indio_dev), struct iio_dev_opaque, indio_dev)

#endif
