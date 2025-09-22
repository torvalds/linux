/* $OpenBSD: fuse_file.c,v 1.10 2024/10/31 13:55:21 claudio Exp $ */
/*
 * Copyright (c) 2012-2013 Sylvestre Gallon <ccna.syl@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vnode.h>
#include <sys/fusebuf.h>

#include "fusefs_node.h"
#include "fusefs.h"

int
fusefs_file_open(struct fusefs_mnt *fmp, struct fusefs_node *ip,
    enum fufh_type fufh_type, int flags, int isdir, struct proc *p)
{
	struct fusebuf *fbuf;
	int error = 0;

	if (!fmp->sess_init)
		return (0);

	fbuf = fb_setup(0, ip->i_number,
	    ((isdir) ? FBT_OPENDIR : FBT_OPEN), p);
	fbuf->fb_io_flags = flags;

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		fb_delete(fbuf);
		return (error);
	}

	ip->fufh[fufh_type].fh_id = fbuf->fb_io_fd;
	ip->fufh[fufh_type].fh_type = fufh_type;

	fb_delete(fbuf);
	return (0);
}

int
fusefs_file_close(struct fusefs_mnt *fmp, struct fusefs_node * ip,
    enum fufh_type fufh_type, int flags, int isdir, struct proc *p)
{
	struct fusebuf *fbuf;
	int error = 0;

	if (fmp->sess_init) {
		fbuf = fb_setup(0, ip->i_number,
		    ((isdir) ? FBT_RELEASEDIR : FBT_RELEASE), p);
		fbuf->fb_io_fd  = ip->fufh[fufh_type].fh_id;
		fbuf->fb_io_flags = flags;

		error = fb_queue(fmp->dev, fbuf);
		if (error && (error != ENOSYS))
			printf("fusefs: file error %d\n", error);

		fb_delete(fbuf);
	}

	ip->fufh[fufh_type].fh_id = (uint64_t)-1;
	ip->fufh[fufh_type].fh_type = FUFH_INVALID;

	return (error);
}

uint64_t
fusefs_fd_get(struct fusefs_node *ip, enum fufh_type type)
{
	if (ip->fufh[type].fh_type == FUFH_INVALID)
		type = FUFH_RDWR;

	return (ip->fufh[type].fh_id);
}
