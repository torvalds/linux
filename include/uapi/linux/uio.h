/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *	Berkeley style UIO structures	-	Alan Cox 1994.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _UAPI__LINUX_UIO_H
#define _UAPI__LINUX_UIO_H

#include <linux/compiler.h>
#include <linux/types.h>


struct iovec
{
	void __user *iov_base;	/* BSD uses caddr_t (1003.1g requires void *) */
	__kernel_size_t iov_len; /* Must be size_t (1003.1g) */
};

struct dmabuf_cmsg {
	__u64 frag_offset;	/* offset into the dmabuf where the frag starts.
				 */
	__u32 frag_size;	/* size of the frag. */
	__u32 frag_token;	/* token representing this frag for
				 * DEVMEM_DONTNEED.
				 */
	__u32  dmabuf_id;	/* dmabuf id this frag belongs to. */
	__u32 flags;		/* Currently unused. Reserved for future
				 * uses.
				 */
};

/*
 *	UIO_MAXIOV shall be at least 16 1003.1g (5.4.1.1)
 */
 
#define UIO_FASTIOV	8
#define UIO_MAXIOV	1024


#endif /* _UAPI__LINUX_UIO_H */
