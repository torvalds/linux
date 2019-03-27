/*-
 * Copyright (c) 2011 Google, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>

#include "libuserboot.h"

ssize_t
userboot_copyin(const void *src, vm_offset_t va, size_t len)
{

        CALLBACK(copyin, src, va, len);
	return (len);
}

ssize_t
userboot_copyout(vm_offset_t va, void *dst, size_t len)
{

	CALLBACK(copyout, va, dst, len);
	return (len);
}

ssize_t
userboot_readin(int fd, vm_offset_t va, size_t len)
{
	ssize_t res, s;
	size_t sz;
	char buf[4096];

	res = 0;
	while (len > 0) {
		sz = len;
		if (sz > sizeof(buf))
			sz = sizeof(buf);
		s = read(fd, buf, sz);
		if (s == 0)
			break;
		if (s < 0)
			return (s);
		CALLBACK(copyin, buf, va, s);
		len -= s;
		res += s;
		va += s;
	}
	return (res);
}
