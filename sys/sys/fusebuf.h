/* $OpenBSD: fusebuf.h,v 1.16 2025/09/06 06:15:52 helg Exp $ */
/*
 * Copyright (c) 2013 Sylvestre Gallon
 * Copyright (c) 2013 Martin Pieuchot
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

#ifndef _SYS_FUSEBUF_H_
#define _SYS_FUSEBUF_H_

/*
 * Maximum size of the read or write buffer sent from the kernel for VFS
 * syscalls: read, readdir, readlink, write.
 */
#define FUSEBUFMAXSIZE	(4096*1024)

/* header at beginning of each fusebuf: */
struct fb_hdr {
	SIMPLEQ_ENTRY(fusebuf)	fh_next;	/* next buffer in chain */
	size_t			fh_len;		/* Amount of data */
	int			fh_err;		/* errno to pass back */
	int			fh_type;	/* type of data */
	ino_t			fh_ino;		/* Inode of this fusebuf */
	uint64_t		fh_uuid;	/* Uuid to track the answer */
	pid_t			fh_tid;		/* calling proc thread id */
	uid_t			fh_uid;		/* calling proc uid */
	gid_t			fh_gid;		/* calling proc gid */
	mode_t			fh_umask;	/* calling proc umask */
};

/* header for fuse file operations (like read/write/mkdir): */
struct fb_io {
	uint64_t	fi_fd;		/* fd where the io is performed */
	ino_t           fi_ino;		/* ino for the io */
	off_t		fi_off;		/* offset for the io */
	size_t		fi_len;		/* Length of data */
	mode_t		fi_mode;	/* mode for fd */
	uint32_t	fi_flags;	/* flags on transfer */
	dev_t		fi_rdev;	/* dev for mknod */
};

/*
 * An operation is issued by the kernel through fuse(4) when the
 * userland file system needs to execute an action (mkdir(2),
 * link(2), etc).
 *
 * F_databuf can be superior to FUSELEN for fusefs_read, fusefs_writes and
 * fusefs_readdir. If it is the case the transfer will be split in N
 * fusebuf with a changing offset in FD_io.
 *
 * When the userland file system answers to this operation it uses
 * the same ID (fh_uuid).
 */
struct fusebuf {
	struct fb_hdr	fb_hdr;
	union {
		struct statvfs	FD_stat;	/* vfs statfs */
		struct stat	FD_attr;	/* for attr vnops */
		struct fb_io	FD_io;		/* for file io vnops */
	} FD;
	uint8_t *fb_dat;			/* data's */
};

#define fb_next		fb_hdr.fh_next
#define fb_len		fb_hdr.fh_len
#define fb_err		fb_hdr.fh_err
#define fb_type		fb_hdr.fh_type
#define fb_ino		fb_hdr.fh_ino
#define fb_uuid		fb_hdr.fh_uuid
#define fb_tid		fb_hdr.fh_tid
#define fb_uid		fb_hdr.fh_uid
#define fb_gid		fb_hdr.fh_gid
#define fb_umask	fb_hdr.fh_umask

#define fb_stat		FD.FD_stat
#define fb_attr		FD.FD_attr
#define fb_io_fd	FD.FD_io.fi_fd
#define fb_io_ino	FD.FD_io.fi_ino
#define fb_io_off	FD.FD_io.fi_off
#define fb_io_len	FD.FD_io.fi_len
#define fb_io_mode	FD.FD_io.fi_mode
#define fb_io_flags	FD.FD_io.fi_flags
#define fb_io_rdev	FD.FD_io.fi_rdev

/*
 * Macros for type conversion
 * fbtod(fb,t) -	convert fusebuf pointer to data pointer of correct
 *			type
 */
#define	fbtod(fb,t)	((t)((fb)->fb_dat))

/* flags needed by setattr */
#define FUSE_FATTR_MODE		(1 << 0)
#define FUSE_FATTR_UID		(1 << 1)
#define FUSE_FATTR_GID		(1 << 2)
#define FUSE_FATTR_SIZE		(1 << 3)
#define FUSE_FATTR_ATIME	(1 << 4)
#define FUSE_FATTR_MTIME	(1 << 5)
#define FUSE_FATTR_FH		(1 << 6)

/* fusebuf types */
#define	FBT_LOOKUP	0
#define FBT_GETATTR	1
#define FBT_SETATTR	2
#define FBT_READLINK	3
#define FBT_SYMLINK	4
#define FBT_MKNOD	5
#define FBT_MKDIR	6
#define FBT_UNLINK	7
#define FBT_RMDIR	8
#define FBT_RENAME	9
#define FBT_LINK	10
#define FBT_OPEN	11
#define FBT_READ	12
#define FBT_WRITE	13
#define FBT_STATFS	14
#define FBT_RELEASE	16
#define FBT_FSYNC	17
#define FBT_FLUSH	18
#define FBT_INIT	19
#define FBT_OPENDIR	20
#define FBT_READDIR	21
#define FBT_RELEASEDIR	22
#define FBT_FSYNCDIR	23
#define FBT_ACCESS	24
#define FBT_DESTROY	26
#define FBT_RECLAIM	27

#ifdef _KERNEL

/* fusebuf prototypes */
struct	fusebuf *fb_setup(size_t, ino_t, int, struct proc *);
int	fb_queue(dev_t, struct fusebuf *);
void	fb_delete(struct fusebuf *);

#endif /* _KERNEL */
#endif /* _SYS_FUSEBUF_H_ */
