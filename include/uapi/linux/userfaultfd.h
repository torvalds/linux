/*
 *  include/linux/userfaultfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *  Copyright (C) 2015  Red Hat, Inc.
 *
 */

#ifndef _LINUX_USERFAULTFD_H
#define _LINUX_USERFAULTFD_H

#include <linux/types.h>

#define UFFD_API ((__u64)0xAA)
/* FIXME: add "|UFFD_BIT_WP" to UFFD_API_BITS after implementing it */
#define UFFD_API_BITS (UFFD_BIT_WRITE)
#define UFFD_API_IOCTLS				\
	((__u64)1 << _UFFDIO_REGISTER |		\
	 (__u64)1 << _UFFDIO_UNREGISTER |	\
	 (__u64)1 << _UFFDIO_API)
#define UFFD_API_RANGE_IOCTLS			\
	((__u64)1 << _UFFDIO_WAKE)

/*
 * Valid ioctl command number range with this API is from 0x00 to
 * 0x3F.  UFFDIO_API is the fixed number, everything else can be
 * changed by implementing a different UFFD_API. If sticking to the
 * same UFFD_API more ioctl can be added and userland will be aware of
 * which ioctl the running kernel implements through the ioctl command
 * bitmask written by the UFFDIO_API.
 */
#define _UFFDIO_REGISTER		(0x00)
#define _UFFDIO_UNREGISTER		(0x01)
#define _UFFDIO_WAKE			(0x02)
#define _UFFDIO_API			(0x3F)

/* userfaultfd ioctl ids */
#define UFFDIO 0xAA
#define UFFDIO_API		_IOWR(UFFDIO, _UFFDIO_API,	\
				      struct uffdio_api)
#define UFFDIO_REGISTER		_IOWR(UFFDIO, _UFFDIO_REGISTER, \
				      struct uffdio_register)
#define UFFDIO_UNREGISTER	_IOR(UFFDIO, _UFFDIO_UNREGISTER,	\
				     struct uffdio_range)
#define UFFDIO_WAKE		_IOR(UFFDIO, _UFFDIO_WAKE,	\
				     struct uffdio_range)

/*
 * Valid bits below PAGE_SHIFT in the userfault address read through
 * the read() syscall.
 */
#define UFFD_BIT_WRITE	(1<<0)	/* this was a write fault, MISSING or WP */
#define UFFD_BIT_WP	(1<<1)	/* handle_userfault() reason VM_UFFD_WP */
#define UFFD_BITS	2	/* two above bits used for UFFD_BIT_* mask */

struct uffdio_api {
	/* userland asks for an API number */
	__u64 api;

	/* kernel answers below with the available features for the API */
	__u64 bits;
	__u64 ioctls;
};

struct uffdio_range {
	__u64 start;
	__u64 len;
};

struct uffdio_register {
	struct uffdio_range range;
#define UFFDIO_REGISTER_MODE_MISSING	((__u64)1<<0)
#define UFFDIO_REGISTER_MODE_WP		((__u64)1<<1)
	__u64 mode;

	/*
	 * kernel answers which ioctl commands are available for the
	 * range, keep at the end as the last 8 bytes aren't read.
	 */
	__u64 ioctls;
};

#endif /* _LINUX_USERFAULTFD_H */
