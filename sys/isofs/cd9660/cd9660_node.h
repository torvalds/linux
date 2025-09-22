/*	$OpenBSD: cd9660_node.h,v 1.23 2024/05/13 01:15:53 jsg Exp $	*/
/*	$NetBSD: cd9660_node.h,v 1.15 1997/04/11 21:52:01 kleink Exp $	*/

/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)cd9660_node.h	8.4 (Berkeley) 12/5/94
 */

#include <sys/buf.h>

#define doff_t	u_quad_t

typedef	struct	{
	struct timespec	iso_atime;	/* time of last access */
	struct timespec	iso_mtime;	/* time of last modification */
	struct timespec	iso_ctime;	/* time file changed */
	u_short		iso_mode;	/* files access mode and type */
	uid_t		iso_uid;	/* owner user id */
	gid_t		iso_gid;	/* owner group id */
	short		iso_links;	/* links of file */
	dev_t		iso_rdev;	/* Major/Minor number for special */
} ISO_RRIP_INODE;

struct iso_node {
	struct	iso_node *i_next, **i_prev;	/* hash chain */
	struct	vnode *i_vnode;	/* vnode associated with this inode */
	struct	vnode *i_devvp;	/* vnode for block I/O */
	u_int	i_flag;		/* see below */
	dev_t	i_dev;		/* device where inode resides */
	cdino_t	i_number;	/* the identity of the inode */
				/* we use the actual starting block of the file */
	struct	iso_mnt *i_mnt;	/* filesystem associated with this inode */
	doff_t	i_endoff;	/* end of useful stuff in directory */
	doff_t	i_diroff;	/* offset in dir, where we found last entry */
	doff_t	i_offset;	/* offset of free space in directory */
	cdino_t	i_ino;		/* inode number of found directory */
	struct	rrwlock i_lock;	/* node lock */

	doff_t	iso_extent;	/* extent of file */
	doff_t	i_size;
	/*
	 * Actual start of data file (may be different from iso_extent, if the
	 * file has extended attributes).
	 */
	doff_t	iso_start;

	ISO_RRIP_INODE  inode;
	struct cluster_info i_ci; 
};

#define	i_forw		i_chain[0]
#define	i_back		i_chain[1]

/* flags */
#define	IN_ACCESS	0x0020		/* inode access time to be updated */

#define VTOI(vp) ((struct iso_node *)(vp)->v_data)
#define ITOV(ip) ((ip)->i_vnode)

/*
 * Prototypes for ISOFS vnode operations
 */
int	cd9660_lookup(void *);
int	cd9660_open(void *);
int	cd9660_close(void *);
int	cd9660_access(void *);
int	cd9660_getattr(void *);
int	cd9660_setattr(void *);
int	cd9660_read(void *);
int	cd9660_ioctl(void *);
int	cd9660_mmap(void *);
int	cd9660_seek(void *);
int	cd9660_readdir(void *);
int	cd9660_readlink(void *);
int	cd9660_inactive(void *);
int	cd9660_reclaim(void *);
int	cd9660_link(void *);
int	cd9660_symlink(void *);
int	cd9660_bmap(void *);
int	cd9660_lock(void *);
int	cd9660_unlock(void *);
int	cd9660_strategy(void *);
int	cd9660_print(void *);
int	cd9660_islocked(void *);
int	cd9660_pathconf(void *);

int	cd9660_bufatoff(struct iso_node *, off_t, char **, struct buf **);

void	cd9660_defattr(struct iso_directory_record *, struct iso_node *,
    struct buf *);
void	cd9660_deftstamp(struct iso_directory_record *, struct iso_node *,
    struct buf *);
struct	vnode *cd9660_ihashget(dev_t, cdino_t);
int	cd9660_ihashins(struct iso_node *);
void	cd9660_ihashrem(struct iso_node *);
int	cd9660_tstamp_conv7(u_char *, struct timespec *);
int	cd9660_tstamp_conv17(u_char *, struct timespec *);
int	cd9660_vget_internal(struct mount *, cdino_t, struct vnode **, int,
    struct iso_directory_record *);
