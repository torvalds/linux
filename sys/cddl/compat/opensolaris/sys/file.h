/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_FILE_H_
#define	_OPENSOLARIS_SYS_FILE_H_

#include_next <sys/file.h>

#define	FKIOCTL	0x80000000	/* ioctl addresses are from kernel */

#ifdef _KERNEL
typedef	struct file	file_t;

#include <sys/capsicum.h>

static __inline file_t *
getf(int fd, cap_rights_t *rightsp)
{
	struct file *fp;

	if (fget(curthread, fd, rightsp, &fp) == 0)
		return (fp);
	return (NULL);
}

static __inline void
releasef(int fd)
{
	struct file *fp;

	/* No CAP_ rights required, as we're only releasing. */
	if (fget(curthread, fd, &cap_no_rights, &fp) == 0) {
		fdrop(fp, curthread);
		fdrop(fp, curthread);
	}
}
#endif	/* _KERNEL */

#endif	/* !_OPENSOLARIS_SYS_FILE_H_ */
