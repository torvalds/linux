/*
 * Copyright (c) 2004-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_IOCTL32_H__
#define __XFS_IOCTL32_H__

#include <linux/compat.h>

/*
 * on 32-bit arches, ioctl argument structures may have different sizes
 * and/or alignment.  We define compat structures which match the
 * 32-bit sizes/alignments here, and their associated ioctl numbers.
 *
 * xfs_ioctl32.c contains routines to copy these structures in and out.
 */

/* stock kernel-level ioctls we support */
#define XFS_IOC_GETXFLAGS_32	FS_IOC32_GETFLAGS
#define XFS_IOC_SETXFLAGS_32	FS_IOC32_SETFLAGS
#define XFS_IOC_GETVERSION_32	FS_IOC32_GETVERSION

/*
 * On intel, even if sizes match, alignment and/or padding may differ.
 */
#if defined(CONFIG_IA64) || defined(CONFIG_X86_64)
#define BROKEN_X86_ALIGNMENT
#define __compat_packed __attribute__((packed))
#else
#define __compat_packed
#endif

typedef struct compat_xfs_bstime {
	compat_time_t	tv_sec;		/* seconds		*/
	__s32		tv_nsec;	/* and nanoseconds	*/
} compat_xfs_bstime_t;

typedef struct compat_xfs_bstat {
	__u64		bs_ino;		/* inode number			*/
	__u16		bs_mode;	/* type and mode		*/
	__u16		bs_nlink;	/* number of links		*/
	__u32		bs_uid;		/* user id			*/
	__u32		bs_gid;		/* group id			*/
	__u32		bs_rdev;	/* device value			*/
	__s32		bs_blksize;	/* block size			*/
	__s64		bs_size;	/* file size			*/
	compat_xfs_bstime_t bs_atime;	/* access time			*/
	compat_xfs_bstime_t bs_mtime;	/* modify time			*/
	compat_xfs_bstime_t bs_ctime;	/* inode change time		*/
	int64_t		bs_blocks;	/* number of blocks		*/
	__u32		bs_xflags;	/* extended flags		*/
	__s32		bs_extsize;	/* extent size			*/
	__s32		bs_extents;	/* number of extents		*/
	__u32		bs_gen;		/* generation count		*/
	__u16		bs_projid_lo;	/* lower part of project id	*/
#define	bs_projid	bs_projid_lo	/* (previously just bs_projid)	*/
	__u16		bs_projid_hi;	/* high part of project id	*/
	unsigned char	bs_pad[12];	/* pad space, unused		*/
	__u32		bs_dmevmask;	/* DMIG event mask		*/
	__u16		bs_dmstate;	/* DMIG state info		*/
	__u16		bs_aextents;	/* attribute number of extents	*/
} __compat_packed compat_xfs_bstat_t;

typedef struct compat_xfs_fsop_bulkreq {
	compat_uptr_t	lastip;		/* last inode # pointer		*/
	__s32		icount;		/* count of entries in buffer	*/
	compat_uptr_t	ubuffer;	/* user buffer for inode desc.	*/
	compat_uptr_t	ocount;		/* output count pointer		*/
} compat_xfs_fsop_bulkreq_t;

#define XFS_IOC_FSBULKSTAT_32 \
	_IOWR('X', 101, struct compat_xfs_fsop_bulkreq)
#define XFS_IOC_FSBULKSTAT_SINGLE_32 \
	_IOWR('X', 102, struct compat_xfs_fsop_bulkreq)
#define XFS_IOC_FSINUMBERS_32 \
	_IOWR('X', 103, struct compat_xfs_fsop_bulkreq)

typedef struct compat_xfs_fsop_handlereq {
	__u32		fd;		/* fd for FD_TO_HANDLE		*/
	compat_uptr_t	path;		/* user pathname		*/
	__u32		oflags;		/* open flags			*/
	compat_uptr_t	ihandle;	/* user supplied handle		*/
	__u32		ihandlen;	/* user supplied length		*/
	compat_uptr_t	ohandle;	/* user buffer for handle	*/
	compat_uptr_t	ohandlen;	/* user buffer length		*/
} compat_xfs_fsop_handlereq_t;

#define XFS_IOC_PATH_TO_FSHANDLE_32 \
	_IOWR('X', 104, struct compat_xfs_fsop_handlereq)
#define XFS_IOC_PATH_TO_HANDLE_32 \
	_IOWR('X', 105, struct compat_xfs_fsop_handlereq)
#define XFS_IOC_FD_TO_HANDLE_32 \
	_IOWR('X', 106, struct compat_xfs_fsop_handlereq)
#define XFS_IOC_OPEN_BY_HANDLE_32 \
	_IOWR('X', 107, struct compat_xfs_fsop_handlereq)
#define XFS_IOC_READLINK_BY_HANDLE_32 \
	_IOWR('X', 108, struct compat_xfs_fsop_handlereq)

/* The bstat field in the swapext struct needs translation */
typedef struct compat_xfs_swapext {
	__int64_t		sx_version;	/* version */
	__int64_t		sx_fdtarget;	/* fd of target file */
	__int64_t		sx_fdtmp;	/* fd of tmp file */
	xfs_off_t		sx_offset;	/* offset into file */
	xfs_off_t		sx_length;	/* leng from offset */
	char			sx_pad[16];	/* pad space, unused */
	compat_xfs_bstat_t	sx_stat;	/* stat of target b4 copy */
} __compat_packed compat_xfs_swapext_t;

#define XFS_IOC_SWAPEXT_32	_IOWR('X', 109, struct compat_xfs_swapext)

typedef struct compat_xfs_fsop_attrlist_handlereq {
	struct compat_xfs_fsop_handlereq hreq; /* handle interface structure */
	struct xfs_attrlist_cursor	pos; /* opaque cookie, list offset */
	__u32				flags;	/* which namespace to use */
	__u32				buflen;	/* length of buffer supplied */
	compat_uptr_t			buffer;	/* returned names */
} __compat_packed compat_xfs_fsop_attrlist_handlereq_t;

/* Note: actually this is read/write */
#define XFS_IOC_ATTRLIST_BY_HANDLE_32 \
	_IOW('X', 122, struct compat_xfs_fsop_attrlist_handlereq)

/* am_opcodes defined in xfs_fs.h */
typedef struct compat_xfs_attr_multiop {
	__u32		am_opcode;
	__s32		am_error;
	compat_uptr_t	am_attrname;
	compat_uptr_t	am_attrvalue;
	__u32		am_length;
	__u32		am_flags;
} compat_xfs_attr_multiop_t;

typedef struct compat_xfs_fsop_attrmulti_handlereq {
	struct compat_xfs_fsop_handlereq hreq; /* handle interface structure */
	__u32				opcount;/* count of following multiop */
	/* ptr to compat_xfs_attr_multiop */
	compat_uptr_t			ops; /* attr_multi data */
} compat_xfs_fsop_attrmulti_handlereq_t;

#define XFS_IOC_ATTRMULTI_BY_HANDLE_32 \
	_IOW('X', 123, struct compat_xfs_fsop_attrmulti_handlereq)

typedef struct compat_xfs_fsop_setdm_handlereq {
	struct compat_xfs_fsop_handlereq hreq;	/* handle information   */
	/* ptr to struct fsdmidata */
	compat_uptr_t			data;	/* DMAPI data   */
} compat_xfs_fsop_setdm_handlereq_t;

#define XFS_IOC_FSSETDM_BY_HANDLE_32 \
	_IOW('X', 121, struct compat_xfs_fsop_setdm_handlereq)

#ifdef BROKEN_X86_ALIGNMENT
/* on ia32 l_start is on a 32-bit boundary */
typedef struct compat_xfs_flock64 {
	__s16		l_type;
	__s16		l_whence;
	__s64		l_start	__attribute__((packed));
			/* len == 0 means until end of file */
	__s64		l_len __attribute__((packed));
	__s32		l_sysid;
	__u32		l_pid;
	__s32		l_pad[4];	/* reserve area */
} compat_xfs_flock64_t;

#define XFS_IOC_ALLOCSP_32	_IOW('X', 10, struct compat_xfs_flock64)
#define XFS_IOC_FREESP_32	_IOW('X', 11, struct compat_xfs_flock64)
#define XFS_IOC_ALLOCSP64_32	_IOW('X', 36, struct compat_xfs_flock64)
#define XFS_IOC_FREESP64_32	_IOW('X', 37, struct compat_xfs_flock64)
#define XFS_IOC_RESVSP_32	_IOW('X', 40, struct compat_xfs_flock64)
#define XFS_IOC_UNRESVSP_32	_IOW('X', 41, struct compat_xfs_flock64)
#define XFS_IOC_RESVSP64_32	_IOW('X', 42, struct compat_xfs_flock64)
#define XFS_IOC_UNRESVSP64_32	_IOW('X', 43, struct compat_xfs_flock64)
#define XFS_IOC_ZERO_RANGE_32	_IOW('X', 57, struct compat_xfs_flock64)

typedef struct compat_xfs_fsop_geom_v1 {
	__u32		blocksize;	/* filesystem (data) block size */
	__u32		rtextsize;	/* realtime extent size		*/
	__u32		agblocks;	/* fsblocks in an AG		*/
	__u32		agcount;	/* number of allocation groups	*/
	__u32		logblocks;	/* fsblocks in the log		*/
	__u32		sectsize;	/* (data) sector size, bytes	*/
	__u32		inodesize;	/* inode size in bytes		*/
	__u32		imaxpct;	/* max allowed inode space(%)	*/
	__u64		datablocks;	/* fsblocks in data subvolume	*/
	__u64		rtblocks;	/* fsblocks in realtime subvol	*/
	__u64		rtextents;	/* rt extents in realtime subvol*/
	__u64		logstart;	/* starting fsblock of the log	*/
	unsigned char	uuid[16];	/* unique id of the filesystem	*/
	__u32		sunit;		/* stripe unit, fsblocks	*/
	__u32		swidth;		/* stripe width, fsblocks	*/
	__s32		version;	/* structure version		*/
	__u32		flags;		/* superblock version flags	*/
	__u32		logsectsize;	/* log sector size, bytes	*/
	__u32		rtsectsize;	/* realtime sector size, bytes	*/
	__u32		dirblocksize;	/* directory block size, bytes	*/
} __attribute__((packed)) compat_xfs_fsop_geom_v1_t;

#define XFS_IOC_FSGEOMETRY_V1_32  \
	_IOR('X', 100, struct compat_xfs_fsop_geom_v1)

typedef struct compat_xfs_inogrp {
	__u64		xi_startino;	/* starting inode number	*/
	__s32		xi_alloccount;	/* # bits set in allocmask	*/
	__u64		xi_allocmask;	/* mask of allocated inodes	*/
} __attribute__((packed)) compat_xfs_inogrp_t;

/* These growfs input structures have padding on the end, so must translate */
typedef struct compat_xfs_growfs_data {
	__u64		newblocks;	/* new data subvol size, fsblocks */
	__u32		imaxpct;	/* new inode space percentage limit */
} __attribute__((packed)) compat_xfs_growfs_data_t;

typedef struct compat_xfs_growfs_rt {
	__u64		newblocks;	/* new realtime size, fsblocks */
	__u32		extsize;	/* new realtime extent size, fsblocks */
} __attribute__((packed)) compat_xfs_growfs_rt_t;

#define XFS_IOC_FSGROWFSDATA_32 _IOW('X', 110, struct compat_xfs_growfs_data)
#define XFS_IOC_FSGROWFSRT_32   _IOW('X', 112, struct compat_xfs_growfs_rt)

#endif /* BROKEN_X86_ALIGNMENT */

#endif /* __XFS_IOCTL32_H__ */
