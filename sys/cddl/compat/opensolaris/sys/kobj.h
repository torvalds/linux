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

#ifndef _OPENSOLARIS_SYS_KOBJ_H_
#define	_OPENSOLARIS_SYS_KOBJ_H_

#include <sys/types.h>
#include <sys/kmem.h>
#include_next <sys/kobj.h>
#ifdef AT_UID
#undef AT_UID
#endif
#ifdef AT_GID
#undef AT_GID
#endif
#include <sys/vnode.h>

#define	KM_NOWAIT	0x01
#define	KM_TMP		0x02

void kobj_free(void *address, size_t size);
void *kobj_alloc(size_t size, int flag);
void *kobj_zalloc(size_t size, int flag);

struct _buf {
	void *ptr;
	int mounted;
};

struct _buf *kobj_open_file(const char *path);
int kobj_get_filesize(struct _buf *file, uint64_t *size);
int kobj_read_file(struct _buf *file, char *buf, unsigned size, unsigned off);
void kobj_close_file(struct _buf *file);

#endif	/* _OPENSOLARIS_SYS_KOBJ_H_ */
