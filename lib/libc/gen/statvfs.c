/*	$OpenBSD: statvfs.c,v 1.2 2015/01/16 16:48:51 deraadt Exp $	*/

/*
 * Copyright (c) 2008 Otto Moerbeek <otto@drijf.net>
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

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/statvfs.h>

static void
cvt_statfs_to_statvfs(const struct statfs *fsp, struct statvfs *vfsp)
{
	vfsp->f_bsize = fsp->f_iosize; /* XXX */
	vfsp->f_frsize = fsp->f_bsize; /* XXX */

	vfsp->f_blocks = fsp->f_blocks;
	vfsp->f_bfree = fsp->f_bfree;
	vfsp->f_bavail = fsp->f_bavail < 0 ? 0 : fsp->f_bavail;

	vfsp->f_files = fsp->f_files;
	vfsp->f_ffree = fsp->f_ffree;
	vfsp->f_favail = fsp->f_favail < 0 ? 0 : fsp->f_favail;

	vfsp->f_fsid = fsp->f_fsid.val[0]; /* XXX */
	vfsp->f_flag = 0;
	if (fsp->f_flags & MNT_RDONLY)
		vfsp->f_flag |= ST_RDONLY;
	if (fsp->f_flags & MNT_NOSUID)
		vfsp->f_flag |= ST_NOSUID;
	vfsp->f_namemax = fsp->f_namemax;
}

int
fstatvfs(int fd, struct statvfs *p)
{
	struct statfs sb;
	int ret;

	ret = fstatfs(fd, &sb);
	if (ret == 0)
		cvt_statfs_to_statvfs(&sb, p);
	return (ret);
}

int
statvfs(const char *path, struct statvfs *p)
{
	struct statfs sb;
	int ret;

	ret = statfs(path, &sb);
	if (ret == 0)
		cvt_statfs_to_statvfs(&sb, p);
	return (ret);
}

