/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _INDUSTRIAL_IO_OPAQUE_H_
#define _INDUSTRIAL_IO_OPAQUE_H_

/**
 * struct iio_dev_opaque - industrial I/O device opaque information
 * @indio_dev:			public industrial I/O device information
 * @debugfs_dentry:		device specific debugfs dentry
 * @cached_reg_addr:		cached register address for debugfs reads
 * @read_buf:			read buffer to be used for the initial reg read
 * @read_buf_len:		data length in @read_buf
 */
struct iio_dev_opaque {
	struct iio_dev			indio_dev;
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
