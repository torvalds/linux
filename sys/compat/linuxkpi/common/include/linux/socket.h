/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_SOCKET_H_
#define	_LINUX_SOCKET_H_

#include <sys/socket.h>

#ifdef notyet
static inline int
memcpy_toiovec(struct iovec *v, unsigned char *kdata, int len)
{
	struct uio uio;
	int error;

	uio.uio_iov = v;
	uio.uio_iovcnt = -1;
	uio.uio_offset = 0;
	uio.uio_resid = len;
	uio.uio_segflag = UIO_USERSPACE;
	uio.uio_rw = UIO_READ;
	error = -uiomove(kdata, len, &uio);
	return (error);
}

static inline int
memcpy_fromiovec(unsigned char *kdata, struct iovec *iov, int len)
{
	struct uio uio;
	int error;

	uio.uio_iov = v;
	uio.uio_iovcnt = -1;
	uio.uio_offset = 0;
	uio.uio_resid = len;
	uio.uio_segflag = UIO_USERSPACE;
	uio.uio_rw = UIO_WRITE;
	error = -uiomove(kdata, len, &uio);
}
#endif

#endif	/* _LINUX_SOCKET_H_ */
