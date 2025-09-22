/*	$OpenBSD: statvfs.h,v 1.4 2022/02/11 15:11:35 millert Exp $	*/

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

#ifndef	_SYS_STATVFS_H_
#define	_SYS_STATVFS_H_

#include <sys/types.h>

struct statvfs {
	unsigned long	f_bsize;	/* file system block size */
	unsigned long	f_frsize;	/* fundamental file system block size */
	fsblkcnt_t	f_blocks;	/* number of blocks (unit f_frsize) */
	fsblkcnt_t	f_bfree;	/* free blocks in file system */
	fsblkcnt_t	f_bavail;	/* free blocks for non-root */
	fsfilcnt_t	f_files;	/* total file inodes */
	fsfilcnt_t	f_ffree;	/* free file inodes */
	fsfilcnt_t	f_favail;	/* free file inodes for non-root */
	unsigned long	f_fsid;		/* file system id */
	unsigned long	f_flag;		/* bit mask of f_flag values */
	unsigned long	f_namemax;	/* maximum filename length */
};

#define ST_RDONLY	0x0001UL	/* read-only filesystem */
#define	ST_NOSUID	0x0002UL	/* nosuid flag set */

#ifndef _KERNEL
__BEGIN_DECLS
int	fstatvfs(int, struct statvfs *);
int	statvfs(const char *, struct statvfs *);
__END_DECLS
#endif /* !_KERNEL */

#endif /* !_SYS_STATVFS_H_ */
