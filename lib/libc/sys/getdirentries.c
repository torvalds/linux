/*-
 * Copyright (c) 2017 M. Warner Losh <imp@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define _WANT_FREEBSD11_DIRENT

#include "namespace.h"
#include <sys/param.h>
#include <sys/syscall.h>
#include "compat-ino64.h"
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "libc_private.h"

static ssize_t
__cvt_dirents_from11(const char *de11, ssize_t len11, char *de, ssize_t len)
{
	struct dirent *dst;
	const struct freebsd11_dirent *src;
	const char *edst, *esrc;
	ssize_t rlen;

	src = (const struct freebsd11_dirent *)de11;
	dst = (struct dirent *)de;
	esrc = de11 + len11;
	edst = de + len;
	while ((const char *)src < esrc && (const char *)dst < edst) {
		rlen = roundup(offsetof(struct dirent, d_name) + src->d_namlen + 1, 8);
		if ((const char *)dst + rlen >= edst)
			break;
		dst->d_fileno = src->d_fileno;
		dst->d_off = 0;			/* nothing uses it yet, so safe for now */
		dst->d_reclen = rlen;
		dst->d_type = src->d_type;
		dst->d_pad0 = 0;
		dst->d_namlen = src->d_namlen;
		dst->d_pad1 = 0;
		memset(dst->d_name, 0, roundup(src->d_namlen + 1, 8));
		memcpy(dst->d_name, src->d_name, src->d_namlen);
		dst = (struct dirent *)((char *)dst + rlen);
		src = (const struct freebsd11_dirent *)((const char *)src + src->d_reclen);
	}
	return ((char *)dst - de);
}

#undef getdirentries
__weak_reference(_getdirentries, getdirentries);

#pragma weak _getdirentries
ssize_t
_getdirentries(int fd, char *buf, size_t nbytes, off_t *basep)
{
	char *oldbuf;
	size_t len;
	ssize_t rv;

	if (__getosreldate() >= INO64_FIRST)
		return (__sys_getdirentries(fd, buf, nbytes, basep));

	/*
	 * Because the old system call returns entries that are smaller than the
	 * new, we could wind up in a situation where we have too many to fit in
	 * the buffer with the new encoding. So sacrifice a small bit of
	 * efficiency to ensure that never happens. We pick 1/4 the size round
	 * up to the next DIRBLKSIZ. This will guarnatee enough room exists in
	 * the dst buffer due to changes in efficiency in packing dirent
	 * entries. We don't check against minimum block size to avoid a lot of
	 * stat calls, we'll see if that's wise or not.
	 * TBD: Will this difference matter to lseek?
	 */
	len = roundup(nbytes / 4, DIRBLKSIZ);
	oldbuf = malloc(len);
	if (oldbuf == NULL) {
		errno = EINVAL;		/* ENOMEM not in possible list */
		return (-1);
	}
	rv = syscall(SYS_freebsd11_getdirentries, fd, oldbuf, len, basep);
	if (rv == -1) {
		free(oldbuf);
		return (rv);
	}
	if (rv > 0)
		rv = __cvt_dirents_from11(oldbuf, rv, buf, nbytes);
	free(oldbuf);

	return (rv);
}
