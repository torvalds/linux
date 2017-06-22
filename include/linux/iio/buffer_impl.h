#ifndef _IIO_BUFFER_GENERIC_IMPL_H_
#define _IIO_BUFFER_GENERIC_IMPL_H_
#include <linux/sysfs.h>
#include <linux/kref.h>

#ifdef CONFIG_IIO_BUFFER

struct iio_dev;
struct iio_buffer;

/**
 * INDIO_BUFFER_FLAG_FIXED_WATERMARK - Watermark level of the buffer can not be
 *   configured. It has a fixed value which will be buffer specific.
 */
#define INDIO_BUFFER_FLAG_FIXED_WATERMARK BIT(0)

/**
 * struct iio_buffer_access_funcs - access functions for buffers.
 * @store_to:		actually store stuff to the buffer
 * @read_first_n:	try to get a specified number of bytes (must exist)
 * @data_available:	indicates how much data is available for reading from
 *			the buffer.
 * @request_update:	if a parameter change has been marked, update underlying
 *			storage.
 * @set_bytes_per_datum:set number of bytes per datum
 * @set_length:		set number of datums in buffer
 * @enable:             called if the buffer is attached to a device and the
 *                      device starts sampling. Calls are balanced with
 *                      @disable.
 * @disable:            called if the buffer is attached to a device and the
 *                      device stops sampling. Calles are balanced with @enable.
 * @release:		called when the last reference to the buffer is dropped,
 *			should free all resources allocated by the buffer.
 * @modes:		Supported operating modes by this buffer type
 * @flags:		A bitmask combination of INDIO_BUFFER_FLAG_*
 *
 * The purpose of this structure is to make the buffer element
 * modular as event for a given driver, different usecases may require
 * different buffer designs (space efficiency vs speed for example).
 *
 * It is worth noting that a given buffer implementation may only support a
 * small proportion of these functions.  The core code 'should' cope fine with
 * any of them not existing.
 **/
struct iio_buffer_access_funcs {
	int (*store_to)(struct iio_buffer *buffer, const void *data);
	int (*read_first_n)(struct iio_buffer *buffer,
			    size_t n,
			    char __user *buf);
	size_t (*data_available)(struct iio_buffer *buffer);

	int (*request_update)(struct iio_buffer *buffer);

	int (*set_bytes_per_datum)(struct iio_buffer *buffer, size_t bpd);
	int (*set_length)(struct iio_buffer *buffer, int length);

	int (*enable)(struct iio_buffer *buffer, struct iio_dev *indio_dev);
	int (*disable)(struct iio_buffer *buffer, struct iio_dev *indio_dev);

	void (*release)(struct iio_buffer *buffer);

	unsigned int modes;
	unsigned int flags;
};

/**
 * struct iio_buffer - general buffer structure
 *
 * Note that the internals of this structure should only be of interest to
 * those writing new buffer implementations.
 */
struct iio_buffer {
	/** @length: Number of datums in buffer. */
	int length;

	/**  @bytes_per_datum: Size of individual datum including timestamp. */
	int bytes_per_datum;

	/**
	 * @access: Buffer access functions associated with the
	 * implementation.
	 */
	const struct iio_buffer_access_funcs *access;

	/** @scan_mask: Bitmask used in masking scan mode elements. */
	long *scan_mask;

	/** @demux_list: List of operations required to demux the scan. */
	struct list_head demux_list;

	/** @pollq: Wait queue to allow for polling on the buffer. */
	wait_queue_head_t pollq;

	/** @watermark: Number of datums to wait for poll/read. */
	unsigned int watermark;

	/* private: */
	/*
	 * @scan_el_attrs: Control of scan elements if that scan mode
	 * control method is used.
	 */
	struct attribute_group *scan_el_attrs;

	/* @scan_timestamp: Does the scan mode include a timestamp. */
	bool scan_timestamp;

	/* @scan_el_dev_attr_list: List of scan element related attributes. */
	struct list_head scan_el_dev_attr_list;

	/* @buffer_group: Attributes of the buffer group. */
	struct attribute_group buffer_group;

	/*
	 * @scan_el_group: Attribute group for those attributes not
	 * created from the iio_chan_info array.
	 */
	struct attribute_group scan_el_group;

	/* @stufftoread: Flag to indicate new data. */
	bool stufftoread;

	/* @attrs: Standard attributes of the buffer. */
	const struct attribute **attrs;

	/* @demux_bounce: Buffer for doing gather from incoming scan. */
	void *demux_bounce;

	/* @buffer_list: Entry in the devices list of current buffers. */
	struct list_head buffer_list;

	/* @ref: Reference count of the buffer. */
	struct kref ref;
};

/**
 * iio_update_buffers() - add or remove buffer from active list
 * @indio_dev:		device to add buffer to
 * @insert_buffer:	buffer to insert
 * @remove_buffer:	buffer_to_remove
 *
 * Note this will tear down the all buffering and build it up again
 */
int iio_update_buffers(struct iio_dev *indio_dev,
		       struct iio_buffer *insert_buffer,
		       struct iio_buffer *remove_buffer);

/**
 * iio_buffer_init() - Initialize the buffer structure
 * @buffer:		buffer to be initialized
 **/
void iio_buffer_init(struct iio_buffer *buffer);

struct iio_buffer *iio_buffer_get(struct iio_buffer *buffer);
void iio_buffer_put(struct iio_buffer *buffer);

#else /* CONFIG_IIO_BUFFER */

static inline void iio_buffer_get(struct iio_buffer *buffer) {}
static inline void iio_buffer_put(struct iio_buffer *buffer) {}

#endif /* CONFIG_IIO_BUFFER */
#endif /* _IIO_BUFFER_GENERIC_IMPL_H_ */
