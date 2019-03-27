/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf
 * Copyright (c) 2008, 2009 Reinoud Zandijk
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
 *
 * From: NetBSD: nilfs.h,v 1.1 2009/07/18 16:31:42 reinoud
 *
 * $FreeBSD$
 */

#ifndef _FS_NANDFS_NANDFS_H_
#define _FS_NANDFS_NANDFS_H_

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/mutex.h>

#include <sys/disk.h>
#include <sys/kthread.h>
#include "nandfs_fs.h"

MALLOC_DECLARE(M_NANDFSTEMP);

/* Debug categories */
#define	NANDFS_DEBUG_VOLUMES		0x000001
#define	NANDFS_DEBUG_BLOCK		0x000004
#define	NANDFS_DEBUG_LOCKING		0x000008
#define	NANDFS_DEBUG_NODE		0x000010
#define	NANDFS_DEBUG_LOOKUP		0x000020
#define	NANDFS_DEBUG_READDIR		0x000040
#define	NANDFS_DEBUG_TRANSLATE		0x000080
#define	NANDFS_DEBUG_STRATEGY		0x000100
#define	NANDFS_DEBUG_READ		0x000200
#define	NANDFS_DEBUG_WRITE		0x000400
#define	NANDFS_DEBUG_IFILE		0x000800
#define	NANDFS_DEBUG_ATTR		0x001000
#define	NANDFS_DEBUG_EXTATTR		0x002000
#define	NANDFS_DEBUG_ALLOC		0x004000
#define	NANDFS_DEBUG_CPFILE		0x008000
#define	NANDFS_DEBUG_DIRHASH		0x010000
#define	NANDFS_DEBUG_NOTIMPL		0x020000
#define	NANDFS_DEBUG_SHEDULE		0x040000
#define	NANDFS_DEBUG_SEG		0x080000
#define	NANDFS_DEBUG_SYNC		0x100000
#define	NANDFS_DEBUG_PARANOIA		0x200000
#define	NANDFS_DEBUG_VNCALL		0x400000
#define	NANDFS_DEBUG_BUF		0x1000000
#define	NANDFS_DEBUG_BMAP		0x2000000
#define	NANDFS_DEBUG_DAT		0x4000000
#define	NANDFS_DEBUG_GENERIC		0x8000000
#define	NANDFS_DEBUG_CLEAN		0x10000000

extern int nandfs_verbose;

#define	DPRINTF(name, arg) { \
		if (nandfs_verbose & NANDFS_DEBUG_##name) {\
			printf arg;\
		};\
	}
#define	DPRINTFIF(name, cond, arg) { \
		if (nandfs_verbose & NANDFS_DEBUG_##name) { \
			if (cond) printf arg;\
		};\
	}

#define	VFSTONANDFS(mp)    ((struct nandfsmount *)((mp)->mnt_data))
#define	VTON(vp) ((struct nandfs_node *)(vp)->v_data)
#define	NTOV(xp) ((xp)->nn_vnode)

int nandfs_init(struct vfsconf *);
int nandfs_uninit(struct vfsconf *);

extern struct vop_vector nandfs_vnodeops;
extern struct vop_vector nandfs_system_vnodeops;

struct nandfs_node;

/* Structure and derivatives */
struct nandfs_mdt {
	uint32_t	entries_per_block;
	uint32_t	entries_per_group;
	uint32_t	blocks_per_group;
	uint32_t	groups_per_desc_block;	/* desc is super group */
	uint32_t	blocks_per_desc_block;	/* desc is super group */
};

struct nandfs_segment {
	LIST_ENTRY(nandfs_segment) seg_link;

	struct nandfs_device	*fsdev;

	TAILQ_HEAD(, buf)	 segsum;
	TAILQ_HEAD(, buf)	 data;

	uint64_t		 seg_num;
	uint64_t		 seg_next;
	uint64_t		 start_block;
	uint32_t		 num_blocks;

	uint32_t		 nblocks;
	uint32_t		 nbinfos;
	uint32_t		 segsum_blocks;
	uint32_t		 segsum_bytes;
	uint32_t		 bytes_left;
	char			*current_off;
};

struct nandfs_seginfo {
	LIST_HEAD( ,nandfs_segment)	seg_list;
	struct nandfs_segment		*curseg;
	struct nandfs_device		*fsdev;
	uint32_t			blocks;
	uint8_t				reiterate;
};

#define	NANDFS_FSSTOR_FAILED	1
struct nandfs_fsarea {
	int	offset;
	int	flags;
	int	last_used;
};

extern int nandfs_cleaner_enable;
extern int nandfs_cleaner_interval;
extern int nandfs_cleaner_segments;

struct nandfs_device {
	struct vnode		*nd_devvp;
	struct g_consumer	*nd_gconsumer;

	struct thread		*nd_syncer;
	struct thread		*nd_cleaner;
	int			nd_syncer_exit;
	int			nd_cleaner_exit;

	struct nandfs_fsarea	nd_fsarea[NANDFS_NFSAREAS];
	int			nd_last_fsarea;

	STAILQ_HEAD(nandfs_mnts, nandfsmount)	nd_mounts;
	SLIST_ENTRY(nandfs_device)		nd_next_device;

	/* FS structures */
	struct nandfs_fsdata		nd_fsdata;
	struct nandfs_super_block	nd_super;
	struct nandfs_segment_summary	nd_last_segsum;
	struct nandfs_super_root	nd_super_root;
	struct nandfs_node	*nd_dat_node;
	struct nandfs_node	*nd_cp_node;
	struct nandfs_node	*nd_su_node;
	struct nandfs_node	*nd_gc_node;

	struct nandfs_mdt	nd_dat_mdt;
	struct nandfs_mdt	nd_ifile_mdt;

	struct timespec		nd_ts;

	/* Synchronization */
	struct mtx		nd_mutex;
	struct mtx		nd_sync_mtx;
	struct cv		nd_sync_cv;
	struct mtx		nd_clean_mtx;
	struct cv		nd_clean_cv;
	struct lock		nd_seg_const;

	struct nandfs_seginfo	*nd_seginfo;

	/* FS geometry */
	uint64_t		nd_devsize;
	uint64_t		nd_maxfilesize;
	uint32_t		nd_blocksize;
	uint32_t		nd_erasesize;

	uint32_t		nd_devblocksize;

	uint32_t		nd_segs_reserved;

	/* Segment usage */
	uint64_t		nd_clean_segs;
	uint64_t		*nd_free_base;
	uint64_t		nd_free_count;
	uint64_t		nd_dirty_bufs;

	/* Running values */
	uint64_t		nd_seg_sequence;
	uint64_t		nd_seg_num;
	uint64_t		nd_next_seg_num;
	uint64_t		nd_last_pseg;
	uint64_t		nd_last_cno;
	uint64_t		nd_last_ino;
	uint64_t		nd_fakevblk;

	int			nd_mount_state;
	int			nd_refcnt;
	int			nd_syncing;
	int			nd_cleaning;
};

extern SLIST_HEAD(_nandfs_devices, nandfs_device) nandfs_devices;

#define	NANDFS_FORCE_SYNCER	0x1
#define	NANDFS_UMOUNT		0x2

#define	SYNCER_UMOUNT		0x0
#define	SYNCER_VFS_SYNC		0x1
#define	SYNCER_BDFLUSH		0x2
#define	SYNCER_FFORCE		0x3
#define	SYNCER_FSYNC		0x4
#define	SYNCER_ROUPD		0x5

static __inline int
nandfs_writelockflags(struct nandfs_device *fsdev, int flags)
{
	int error = 0;

	if (lockstatus(&fsdev->nd_seg_const) != LK_EXCLUSIVE)
		error = lockmgr(&fsdev->nd_seg_const, flags | LK_SHARED, NULL);

	return (error);
}

static __inline void
nandfs_writeunlock(struct nandfs_device *fsdev)
{

	if (lockstatus(&fsdev->nd_seg_const) != LK_EXCLUSIVE)
		lockmgr(&(fsdev)->nd_seg_const, LK_RELEASE, NULL);
}

#define NANDFS_WRITELOCKFLAGS(fsdev, flags)	nandfs_writelockflags(fsdev, flags)

#define NANDFS_WRITELOCK(fsdev) NANDFS_WRITELOCKFLAGS(fsdev, 0)

#define NANDFS_WRITEUNLOCK(fsdev) nandfs_writeunlock(fsdev)

#define NANDFS_WRITEASSERT(fsdev) lockmgr_assert(&(fsdev)->nd_seg_const, KA_LOCKED)

/* Specific mountpoint; head or a checkpoint/snapshot */
struct nandfsmount {
	STAILQ_ENTRY(nandfsmount) nm_next_mount;

	struct mount		*nm_vfs_mountp;
	struct nandfs_device	*nm_nandfsdev;
	struct nandfs_args	nm_mount_args;
	struct nandfs_node	*nm_ifile_node;

	uint8_t			nm_flags;
	int8_t			nm_ronly;
};

struct nandfs_node {
	struct vnode			*nn_vnode;
	struct nandfsmount		*nn_nmp;
	struct nandfs_device		*nn_nandfsdev;
	struct lockf			*nn_lockf;

	uint64_t			nn_ino;
	struct nandfs_inode		nn_inode;

	uint64_t			nn_diroff;
	uint32_t			nn_flags;
};

#define	IN_ACCESS	0x0001	/* Inode access time update request  */
#define	IN_CHANGE	0x0002	/* Inode change time update request  */
#define	IN_UPDATE	0x0004	/* Inode was written to; update mtime*/
#define	IN_MODIFIED	0x0008	/* node has been modified */
#define	IN_RENAME	0x0010	/* node is being renamed. */

/* File permissions. */
#define	IEXEC		0000100	/* Executable. */
#define	IWRITE		0000200	/* Writeable. */
#define	IREAD		0000400	/* Readable. */
#define	ISVTX		0001000	/* Sticky bit. */
#define	ISGID		0002000	/* Set-gid. */
#define	ISUID		0004000	/* Set-uid. */

#define	PRINT_NODE_FLAGS \
	"\10\1IN_ACCESS\2IN_CHANGE\3IN_UPDATE\4IN_MODIFIED\5IN_RENAME"

#define	NANDFS_GATHER(x) ((x)->b_flags |= B_FS_FLAG1)
#define	NANDFS_UNGATHER(x) ((x)->b_flags &= ~B_FS_FLAG1)
#define	NANDFS_ISGATHERED(x) ((x)->b_flags & B_FS_FLAG1)

#endif /* !_FS_NANDFS_NANDFS_H_ */
