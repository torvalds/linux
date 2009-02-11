/* include/linux/aio_abi.h
 *
 * Copyright 2000,2001,2002 Red Hat.
 *
 * Written by Benjamin LaHaise <bcrl@kvack.org>
 *
 * Distribute under the terms of the GPLv2 (see ../../COPYING) or under 
 * the following terms.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, provided that the above copyright
 * notice appears in all copies.  This software is provided without any
 * warranty, express or implied.  Red Hat makes no representations about
 * the suitability of this software for any purpose.
 *
 * IN NO EVENT SHALL RED HAT BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
 * SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF
 * THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF RED HAT HAS BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * RED HAT DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND
 * RED HAT HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
 * ENHANCEMENTS, OR MODIFICATIONS.
 */
#ifndef __LINUX__AIO_ABI_H
#define __LINUX__AIO_ABI_H

#include <linux/types.h>
#include <asm/byteorder.h>

typedef unsigned long	aio_context_t;

enum {
	IOCB_CMD_PREAD = 0,
	IOCB_CMD_PWRITE = 1,
	IOCB_CMD_FSYNC = 2,
	IOCB_CMD_FDSYNC = 3,
	/* These two are experimental.
	 * IOCB_CMD_PREADX = 4,
	 * IOCB_CMD_POLL = 5,
	 */
	IOCB_CMD_NOOP = 6,
	IOCB_CMD_PREADV = 7,
	IOCB_CMD_PWRITEV = 8,
};

/*
 * Valid flags for the "aio_flags" member of the "struct iocb".
 *
 * IOCB_FLAG_RESFD - Set if the "aio_resfd" member of the "struct iocb"
 *                   is valid.
 */
#define IOCB_FLAG_RESFD		(1 << 0)

/* read() from /dev/aio returns these structures. */
struct io_event {
	__u64		data;		/* the data field from the iocb */
	__u64		obj;		/* what iocb this event came from */
	__s64		res;		/* result code for this event */
	__s64		res2;		/* secondary result */
};

#if defined(__LITTLE_ENDIAN)
#define PADDED(x,y)	x, y
#elif defined(__BIG_ENDIAN)
#define PADDED(x,y)	y, x
#else
#error edit for your odd byteorder.
#endif

/*
 * we always use a 64bit off_t when communicating
 * with userland.  its up to libraries to do the
 * proper padding and aio_error abstraction
 */

struct iocb {
	/* these are internal to the kernel/libc. */
	__u64	aio_data;	/* data to be returned in event's data */
	__u32	PADDED(aio_key, aio_reserved1);
				/* the kernel sets aio_key to the req # */

	/* common fields */
	__u16	aio_lio_opcode;	/* see IOCB_CMD_ above */
	__s16	aio_reqprio;
	__u32	aio_fildes;

	__u64	aio_buf;
	__u64	aio_nbytes;
	__s64	aio_offset;

	/* extra parameters */
	__u64	aio_reserved2;	/* TODO: use this for a (struct sigevent *) */

	/* flags for the "struct iocb" */
	__u32	aio_flags;

	/*
	 * if the IOCB_FLAG_RESFD flag of "aio_flags" is set, this is an
	 * eventfd to signal AIO readiness to
	 */
	__u32	aio_resfd;
}; /* 64 bytes */

#undef IFBIG
#undef IFLITTLE

#endif /* __LINUX__AIO_ABI_H */

