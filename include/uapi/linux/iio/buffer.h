/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* industrial I/O buffer definitions needed both in and out of kernel
 */

#ifndef _UAPI_IIO_BUFFER_H_
#define _UAPI_IIO_BUFFER_H_

#include <linux/types.h>

/* Flags for iio_dmabuf.flags */
#define IIO_BUFFER_DMABUF_CYCLIC		(1 << 0)
#define IIO_BUFFER_DMABUF_SUPPORTED_FLAGS	0x00000001

/**
 * struct iio_dmabuf - Descriptor for a single IIO DMABUF object
 * @fd:		file descriptor of the DMABUF object
 * @flags:	one or more IIO_BUFFER_DMABUF_* flags
 * @bytes_used:	number of bytes used in this DMABUF for the data transfer.
 *		Should generally be set to the DMABUF's size.
 */
struct iio_dmabuf {
	__u32 fd;
	__u32 flags;
	__u64 bytes_used;
};

#define IIO_BUFFER_GET_FD_IOCTL			_IOWR('i', 0x91, int)
#define IIO_BUFFER_DMABUF_ATTACH_IOCTL		_IOW('i', 0x92, int)
#define IIO_BUFFER_DMABUF_DETACH_IOCTL		_IOW('i', 0x93, int)
#define IIO_BUFFER_DMABUF_ENQUEUE_IOCTL		_IOW('i', 0x94, struct iio_dmabuf)

#endif /* _UAPI_IIO_BUFFER_H_ */
