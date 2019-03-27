/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007-2009 Google Inc. and Amit Singh
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Google Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Copyright (C) 2005 Csaba Henk.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _FUSE_FILE_H_
#define _FUSE_FILE_H_

#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/vnode.h>

typedef enum fufh_type {
	FUFH_INVALID = -1,
	FUFH_RDONLY  = 0,
	FUFH_WRONLY  = 1,
	FUFH_RDWR    = 2,
	FUFH_MAXTYPE = 3,
} fufh_type_t;
_Static_assert(FUFH_RDONLY == O_RDONLY, "RDONLY");
_Static_assert(FUFH_WRONLY == O_WRONLY, "WRONLY");
_Static_assert(FUFH_RDWR == O_RDWR, "RDWR");

struct fuse_filehandle {
	uint64_t fh_id;
	fufh_type_t fh_type;
};

#define FUFH_IS_VALID(f)  ((f)->fh_type != FUFH_INVALID)

static inline fufh_type_t
fuse_filehandle_xlate_from_mmap(int fflags)
{
	if (fflags & (PROT_READ | PROT_WRITE))
		return FUFH_RDWR;
	else if (fflags & (PROT_WRITE))
		return FUFH_WRONLY;
	else if ((fflags & PROT_READ) || (fflags & PROT_EXEC))
		return FUFH_RDONLY;
	else
		return FUFH_INVALID;
}

static inline fufh_type_t
fuse_filehandle_xlate_from_fflags(int fflags)
{
	if ((fflags & FREAD) && (fflags & FWRITE))
		return FUFH_RDWR;
	else if (fflags & (FWRITE))
		return FUFH_WRONLY;
	else if (fflags & (FREAD))
		return FUFH_RDONLY;
	else
		panic("FUSE: What kind of a flag is this (%x)?", fflags);
}

static inline int
fuse_filehandle_xlate_to_oflags(fufh_type_t type)
{
	int oflags = -1;

	switch (type) {
	case FUFH_RDONLY:
	case FUFH_WRONLY:
	case FUFH_RDWR:
		oflags = type;
		break;
	default:
		break;
	}

	return oflags;
}

int fuse_filehandle_valid(struct vnode *vp, fufh_type_t fufh_type);
fufh_type_t fuse_filehandle_validrw(struct vnode *vp, fufh_type_t fufh_type);
int fuse_filehandle_get(struct vnode *vp, fufh_type_t fufh_type,
                        struct fuse_filehandle **fufhp);
int fuse_filehandle_getrw(struct vnode *vp, fufh_type_t fufh_type,
                          struct fuse_filehandle **fufhp);

void fuse_filehandle_init(struct vnode *vp, fufh_type_t fufh_type,
		          struct fuse_filehandle **fufhp, uint64_t fh_id);
int fuse_filehandle_open(struct vnode *vp, fufh_type_t fufh_type,
                         struct fuse_filehandle **fufhp, struct thread *td,
                         struct ucred *cred);
int fuse_filehandle_close(struct vnode *vp, fufh_type_t fufh_type,
                          struct thread *td, struct ucred *cred);

#endif /* _FUSE_FILE_H_ */
