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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>

#include "fuse.h"
#include "fuse_file.h"
#include "fuse_internal.h"
#include "fuse_ipc.h"
#include "fuse_node.h"

#define FUSE_DEBUG_MODULE FILE
#include "fuse_debug.h"

static int fuse_fh_count = 0;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, filehandle_count, CTLFLAG_RD,
    &fuse_fh_count, 0, "number of open FUSE filehandles");

int
fuse_filehandle_open(struct vnode *vp, fufh_type_t fufh_type,
    struct fuse_filehandle **fufhp, struct thread *td, struct ucred *cred)
{
	struct fuse_dispatcher fdi;
	struct fuse_open_in *foi;
	struct fuse_open_out *foo;

	int err = 0;
	int oflags = 0;
	int op = FUSE_OPEN;

	fuse_trace_printf("fuse_filehandle_open(vp=%p, fufh_type=%d)\n",
	    vp, fufh_type);

	if (fuse_filehandle_valid(vp, fufh_type)) {
		panic("FUSE: filehandle_open called despite valid fufh (type=%d)",
		    fufh_type);
		/* NOTREACHED */
	}
	/*
	 * Note that this means we are effectively FILTERING OUT open() flags.
	 */
	oflags = fuse_filehandle_xlate_to_oflags(fufh_type);

	if (vnode_isdir(vp)) {
		op = FUSE_OPENDIR;
		if (fufh_type != FUFH_RDONLY) {
			printf("FUSE:non-rdonly fh requested for a directory?\n");
			fufh_type = FUFH_RDONLY;
		}
	}
	fdisp_init(&fdi, sizeof(*foi));
	fdisp_make_vp(&fdi, op, vp, td, cred);

	foi = fdi.indata;
	foi->flags = oflags;

	if ((err = fdisp_wait_answ(&fdi))) {
		debug_printf("OUCH ... daemon didn't give fh (err = %d)\n", err);
		if (err == ENOENT) {
			fuse_internal_vnode_disappear(vp);
		}
		goto out;
	}
	foo = fdi.answ;

	fuse_filehandle_init(vp, fufh_type, fufhp, foo->fh);

	/*
	 * For WRONLY opens, force DIRECT_IO.  This is necessary
	 * since writing a partial block through the buffer cache
	 * will result in a read of the block and that read won't
	 * be allowed by the WRONLY open.
	 */
	if (fufh_type == FUFH_WRONLY)
		fuse_vnode_open(vp, foo->open_flags | FOPEN_DIRECT_IO, td);
	else
		fuse_vnode_open(vp, foo->open_flags, td);

out:
	fdisp_destroy(&fdi);
	return err;
}

int
fuse_filehandle_close(struct vnode *vp, fufh_type_t fufh_type,
    struct thread *td, struct ucred *cred)
{
	struct fuse_dispatcher fdi;
	struct fuse_release_in *fri;
	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct fuse_filehandle *fufh = NULL;

	int err = 0;
	int op = FUSE_RELEASE;

	fuse_trace_printf("fuse_filehandle_put(vp=%p, fufh_type=%d)\n",
	    vp, fufh_type);

	fufh = &(fvdat->fufh[fufh_type]);
	if (!FUFH_IS_VALID(fufh)) {
		panic("FUSE: filehandle_put called on invalid fufh (type=%d)",
		    fufh_type);
		/* NOTREACHED */
	}
	if (fuse_isdeadfs(vp)) {
		goto out;
	}
	if (vnode_isdir(vp))
		op = FUSE_RELEASEDIR;
	fdisp_init(&fdi, sizeof(*fri));
	fdisp_make_vp(&fdi, op, vp, td, cred);
	fri = fdi.indata;
	fri->fh = fufh->fh_id;
	fri->flags = fuse_filehandle_xlate_to_oflags(fufh_type);

	err = fdisp_wait_answ(&fdi);
	fdisp_destroy(&fdi);

out:
	atomic_subtract_acq_int(&fuse_fh_count, 1);
	fufh->fh_id = (uint64_t)-1;
	fufh->fh_type = FUFH_INVALID;

	return err;
}

int
fuse_filehandle_valid(struct vnode *vp, fufh_type_t fufh_type)
{
	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct fuse_filehandle *fufh;

	fufh = &(fvdat->fufh[fufh_type]);
	return FUFH_IS_VALID(fufh);
}

/*
 * Check for a valid file handle, first the type requested, but if that
 * isn't valid, try for FUFH_RDWR.
 * Return the FUFH type that is valid or FUFH_INVALID if there are none.
 * This is a variant of fuse_filehandle_vaild() analogous to
 * fuse_filehandle_getrw().
 */
fufh_type_t
fuse_filehandle_validrw(struct vnode *vp, fufh_type_t fufh_type)
{
	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct fuse_filehandle *fufh;

	fufh = &fvdat->fufh[fufh_type];
	if (FUFH_IS_VALID(fufh) != 0)
		return (fufh_type);
	fufh = &fvdat->fufh[FUFH_RDWR];
	if (FUFH_IS_VALID(fufh) != 0)
		return (FUFH_RDWR);
	return (FUFH_INVALID);
}

int
fuse_filehandle_get(struct vnode *vp, fufh_type_t fufh_type,
    struct fuse_filehandle **fufhp)
{
	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct fuse_filehandle *fufh;

	fufh = &(fvdat->fufh[fufh_type]);
	if (!FUFH_IS_VALID(fufh))
		return EBADF;
	if (fufhp != NULL)
		*fufhp = fufh;
	return 0;
}

int
fuse_filehandle_getrw(struct vnode *vp, fufh_type_t fufh_type,
    struct fuse_filehandle **fufhp)
{
	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct fuse_filehandle *fufh;

	fufh = &(fvdat->fufh[fufh_type]);
	if (!FUFH_IS_VALID(fufh)) {
		fufh_type = FUFH_RDWR;
	}
	return fuse_filehandle_get(vp, fufh_type, fufhp);
}

void
fuse_filehandle_init(struct vnode *vp, fufh_type_t fufh_type,
    struct fuse_filehandle **fufhp, uint64_t fh_id)
{
	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct fuse_filehandle *fufh;

	FS_DEBUG("id=%jd type=%d\n", (intmax_t)fh_id, fufh_type);
	fufh = &(fvdat->fufh[fufh_type]);
	MPASS(!FUFH_IS_VALID(fufh));
	fufh->fh_id = fh_id;
	fufh->fh_type = fufh_type;
	if (!FUFH_IS_VALID(fufh)) {
		panic("FUSE: init: invalid filehandle id (type=%d)", fufh_type);
	}
	if (fufhp != NULL)
		*fufhp = fufh;

	atomic_add_acq_int(&fuse_fh_count, 1);
}
