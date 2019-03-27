/* $FreeBSD$ */
/*-
 * Copyright (c) 2014 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _CUSE_IOCTL_H_
#define	_CUSE_IOCTL_H_

#include <sys/ioccom.h>
#include <sys/types.h>

#define	CUSE_BUFFER_MAX		PAGE_SIZE
#define	CUSE_DEVICES_MAX	64	/* units */
#define	CUSE_BUF_MIN_PTR	0x10000UL
#define	CUSE_BUF_MAX_PTR	0x20000UL
#define	CUSE_ALLOC_UNIT_MAX	128	/* units */
/* All memory allocations must be less than the following limit */
#define	CUSE_ALLOC_PAGES_MAX	(((16UL * 1024UL * 1024UL) + PAGE_SIZE - 1) / PAGE_SIZE)

struct cuse_dev;

struct cuse_data_chunk {
	uintptr_t local_ptr;
	uintptr_t peer_ptr;
	unsigned long length;
};

struct cuse_alloc_info {
	unsigned long page_count;
	unsigned long alloc_nr;
};

struct cuse_command {
	struct cuse_dev *dev;
	unsigned long fflags;
	uintptr_t per_file_handle;
	uintptr_t data_pointer;
	unsigned long argument;
	unsigned long command;		/* see CUSE_CMD_XXX */
};

struct cuse_create_dev {
	struct cuse_dev *dev;
	uid_t	user_id;
	gid_t	group_id;
	int	permissions;
	char	devname[80];		/* /dev/xxxxx */
};

/* Definition of internal IOCTLs for /dev/cuse */

#define	CUSE_IOCTL_GET_COMMAND		_IOR('C', 0, struct cuse_command)
#define	CUSE_IOCTL_WRITE_DATA		_IOW('C', 1, struct cuse_data_chunk)
#define	CUSE_IOCTL_READ_DATA		_IOW('C', 2, struct cuse_data_chunk)
#define	CUSE_IOCTL_SYNC_COMMAND		_IOW('C', 3, int)
#define	CUSE_IOCTL_GET_SIG		_IOR('C', 4, int)
#define	CUSE_IOCTL_ALLOC_MEMORY		_IOW('C', 5, struct cuse_alloc_info)
#define	CUSE_IOCTL_FREE_MEMORY		_IOW('C', 6, struct cuse_alloc_info)
#define	CUSE_IOCTL_SET_PFH		_IOW('C', 7, uintptr_t)
#define	CUSE_IOCTL_CREATE_DEV		_IOW('C', 8, struct cuse_create_dev)
#define	CUSE_IOCTL_DESTROY_DEV		_IOW('C', 9, struct cuse_dev *)
#define	CUSE_IOCTL_ALLOC_UNIT		_IOR('C',10, int)
#define	CUSE_IOCTL_FREE_UNIT		_IOW('C',11, int)
#define	CUSE_IOCTL_SELWAKEUP		_IOW('C',12, int)
#define	CUSE_IOCTL_ALLOC_UNIT_BY_ID	_IOWR('C',13, int)
#define	CUSE_IOCTL_FREE_UNIT_BY_ID	_IOWR('C',14, int)

#endif			/* _CUSE_IOCTL_H_ */
