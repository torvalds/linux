/*
 * Copyright (c) 1995-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_FS_H__
#define __XFS_FS_H__

/*
 * SGI's XFS filesystem's major stuff (constants, structures)
 */

/*
 * Direct I/O attribute record used with XFS_IOC_DIOINFO
 * d_miniosz is the min xfer size, xfer size multiple and file seek offset
 * alignment.
 */
#ifndef HAVE_DIOATTR
struct dioattr {
	__u32		d_mem;		/* data buffer memory alignment */
	__u32		d_miniosz;	/* min xfer size		*/
	__u32		d_maxiosz;	/* max xfer size		*/
};
#endif

/*
 * Structure for XFS_IOC_FSGETXATTR[A] and XFS_IOC_FSSETXATTR.
 */
#ifndef HAVE_FSXATTR
struct fsxattr {
	__u32		fsx_xflags;	/* xflags field value (get/set) */
	__u32		fsx_extsize;	/* extsize field value (get/set)*/
	__u32		fsx_nextents;	/* nextents field value (get)	*/
	__u32		fsx_projid;	/* project identifier (get/set) */
	unsigned char	fsx_pad[12];
};
#endif

/*
 * Flags for the bs_xflags/fsx_xflags field
 * There should be a one-to-one correspondence between these flags and the
 * XFS_DIFLAG_s.
 */
#define XFS_XFLAG_REALTIME	0x00000001	/* data in realtime volume */
#define XFS_XFLAG_PREALLOC	0x00000002	/* preallocated file extents */
#define XFS_XFLAG_IMMUTABLE	0x00000008	/* file cannot be modified */
#define XFS_XFLAG_APPEND	0x00000010	/* all writes append */
#define XFS_XFLAG_SYNC		0x00000020	/* all writes synchronous */
#define XFS_XFLAG_NOATIME	0x00000040	/* do not update access time */
#define XFS_XFLAG_NODUMP	0x00000080	/* do not include in backups */
#define XFS_XFLAG_RTINHERIT	0x00000100	/* create with rt bit set */
#define XFS_XFLAG_PROJINHERIT	0x00000200	/* create with parents projid */
#define XFS_XFLAG_NOSYMLINKS	0x00000400	/* disallow symlink creation */
#define XFS_XFLAG_EXTSIZE	0x00000800	/* extent size allocator hint */
#define XFS_XFLAG_EXTSZINHERIT	0x00001000	/* inherit inode extent size */
#define XFS_XFLAG_NODEFRAG	0x00002000  	/* do not defragment */
#define XFS_XFLAG_FILESTREAM	0x00004000	/* use filestream allocator */
#define XFS_XFLAG_HASATTR	0x80000000	/* no DIFLAG for this	*/

/*
 * Structure for XFS_IOC_GETBMAP.
 * On input, fill in bmv_offset and bmv_length of the first structure
 * to indicate the area of interest in the file, and bmv_entries with
 * the number of array elements given back.  The first structure is
 * updated on return to give the offset and length for the next call.
 */
#ifndef HAVE_GETBMAP
struct getbmap {
	__s64		bmv_offset;	/* file offset of segment in blocks */
	__s64		bmv_block;	/* starting block (64-bit daddr_t)  */
	__s64		bmv_length;	/* length of segment, blocks	    */
	__s32		bmv_count;	/* # of entries in array incl. 1st  */
	__s32		bmv_entries;	/* # of entries filled in (output)  */
};
#endif

/*
 *	Structure for XFS_IOC_GETBMAPX.	 Fields bmv_offset through bmv_entries
 *	are used exactly as in the getbmap structure.  The getbmapx structure
 *	has additional bmv_iflags and bmv_oflags fields. The bmv_iflags field
 *	is only used for the first structure.  It contains input flags
 *	specifying XFS_IOC_GETBMAPX actions.  The bmv_oflags field is filled
 *	in by the XFS_IOC_GETBMAPX command for each returned structure after
 *	the first.
 */
#ifndef HAVE_GETBMAPX
struct getbmapx {
	__s64		bmv_offset;	/* file offset of segment in blocks */
	__s64		bmv_block;	/* starting block (64-bit daddr_t)  */
	__s64		bmv_length;	/* length of segment, blocks	    */
	__s32		bmv_count;	/* # of entries in array incl. 1st  */
	__s32		bmv_entries;	/* # of entries filled in (output). */
	__s32		bmv_iflags;	/* input flags (1st structure)	    */
	__s32		bmv_oflags;	/* output flags (after 1st structure)*/
	__s32		bmv_unused1;	/* future use			    */
	__s32		bmv_unused2;	/* future use			    */
};
#endif

/*	bmv_iflags values - set by XFS_IOC_GETBMAPX caller.	*/
#define BMV_IF_ATTRFORK		0x1	/* return attr fork rather than data */
#define BMV_IF_NO_DMAPI_READ	0x2	/* Do not generate DMAPI read event  */
#define BMV_IF_PREALLOC		0x4	/* rtn status BMV_OF_PREALLOC if req */
#define BMV_IF_VALID	(BMV_IF_ATTRFORK|BMV_IF_NO_DMAPI_READ|BMV_IF_PREALLOC)
#ifdef __KERNEL__
#define BMV_IF_EXTENDED 0x40000000	/* getpmapx if set */
#endif

/*	bmv_oflags values - returned for for each non-header segment */
#define BMV_OF_PREALLOC		0x1	/* segment = unwritten pre-allocation */

/*	Convert getbmap <-> getbmapx - move fields from p1 to p2. */
#define GETBMAP_CONVERT(p1,p2) {	\
	p2.bmv_offset = p1.bmv_offset;	\
	p2.bmv_block = p1.bmv_block;	\
	p2.bmv_length = p1.bmv_length;	\
	p2.bmv_count = p1.bmv_count;	\
	p2.bmv_entries = p1.bmv_entries;  }


/*
 * Structure for XFS_IOC_FSSETDM.
 * For use by backup and restore programs to set the XFS on-disk inode
 * fields di_dmevmask and di_dmstate.  These must be set to exactly and
 * only values previously obtained via xfs_bulkstat!  (Specifically the
 * xfs_bstat_t fields bs_dmevmask and bs_dmstate.)
 */
#ifndef HAVE_FSDMIDATA
struct fsdmidata {
	__u32		fsd_dmevmask;	/* corresponds to di_dmevmask */
	__u16		fsd_padding;
	__u16		fsd_dmstate;	/* corresponds to di_dmstate  */
};
#endif

/*
 * File segment locking set data type for 64 bit access.
 * Also used for all the RESV/FREE interfaces.
 */
typedef struct xfs_flock64 {
	__s16		l_type;
	__s16		l_whence;
	__s64		l_start;
	__s64		l_len;		/* len == 0 means until end of file */
	__s32		l_sysid;
	__u32		l_pid;
	__s32		l_pad[4];	/* reserve area			    */
} xfs_flock64_t;

/*
 * Output for XFS_IOC_FSGEOMETRY_V1
 */
typedef struct xfs_fsop_geom_v1 {
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
} xfs_fsop_geom_v1_t;

/*
 * Output for XFS_IOC_FSGEOMETRY
 */
typedef struct xfs_fsop_geom {
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
	__u32		logsunit;	/* log stripe unit, bytes */
} xfs_fsop_geom_t;

/* Output for XFS_FS_COUNTS */
typedef struct xfs_fsop_counts {
	__u64	freedata;	/* free data section blocks */
	__u64	freertx;	/* free rt extents */
	__u64	freeino;	/* free inodes */
	__u64	allocino;	/* total allocated inodes */
} xfs_fsop_counts_t;

/* Input/Output for XFS_GET_RESBLKS and XFS_SET_RESBLKS */
typedef struct xfs_fsop_resblks {
	__u64  resblks;
	__u64  resblks_avail;
} xfs_fsop_resblks_t;

#define XFS_FSOP_GEOM_VERSION	0

#define XFS_FSOP_GEOM_FLAGS_ATTR	0x0001	/* attributes in use	*/
#define XFS_FSOP_GEOM_FLAGS_NLINK	0x0002	/* 32-bit nlink values	*/
#define XFS_FSOP_GEOM_FLAGS_QUOTA	0x0004	/* quotas enabled	*/
#define XFS_FSOP_GEOM_FLAGS_IALIGN	0x0008	/* inode alignment	*/
#define XFS_FSOP_GEOM_FLAGS_DALIGN	0x0010	/* large data alignment */
#define XFS_FSOP_GEOM_FLAGS_SHARED	0x0020	/* read-only shared	*/
#define XFS_FSOP_GEOM_FLAGS_EXTFLG	0x0040	/* special extent flag	*/
#define XFS_FSOP_GEOM_FLAGS_DIRV2	0x0080	/* directory version 2	*/
#define XFS_FSOP_GEOM_FLAGS_LOGV2	0x0100	/* log format version 2	*/
#define XFS_FSOP_GEOM_FLAGS_SECTOR	0x0200	/* sector sizes >1BB	*/
#define XFS_FSOP_GEOM_FLAGS_ATTR2	0x0400	/* inline attributes rework */
#define XFS_FSOP_GEOM_FLAGS_DIRV2CI	0x1000	/* ASCII only CI names */
#define XFS_FSOP_GEOM_FLAGS_LAZYSB	0x4000	/* lazy superblock counters */


/*
 * Minimum and maximum sizes need for growth checks
 */
#define XFS_MIN_AG_BLOCKS	64
#define XFS_MIN_LOG_BLOCKS	512
#define XFS_MAX_LOG_BLOCKS	(64 * 1024)
#define XFS_MIN_LOG_BYTES	(256 * 1024)
#define XFS_MAX_LOG_BYTES	(128 * 1024 * 1024)

/*
 * Structures for XFS_IOC_FSGROWFSDATA, XFS_IOC_FSGROWFSLOG & XFS_IOC_FSGROWFSRT
 */
typedef struct xfs_growfs_data {
	__u64		newblocks;	/* new data subvol size, fsblocks */
	__u32		imaxpct;	/* new inode space percentage limit */
} xfs_growfs_data_t;

typedef struct xfs_growfs_log {
	__u32		newblocks;	/* new log size, fsblocks */
	__u32		isint;		/* 1 if new log is internal */
} xfs_growfs_log_t;

typedef struct xfs_growfs_rt {
	__u64		newblocks;	/* new realtime size, fsblocks */
	__u32		extsize;	/* new realtime extent size, fsblocks */
} xfs_growfs_rt_t;


/*
 * Structures returned from ioctl XFS_IOC_FSBULKSTAT & XFS_IOC_FSBULKSTAT_SINGLE
 */
typedef struct xfs_bstime {
	time_t		tv_sec;		/* seconds		*/
	__s32		tv_nsec;	/* and nanoseconds	*/
} xfs_bstime_t;

typedef struct xfs_bstat {
	__u64		bs_ino;		/* inode number			*/
	__u16		bs_mode;	/* type and mode		*/
	__u16		bs_nlink;	/* number of links		*/
	__u32		bs_uid;		/* user id			*/
	__u32		bs_gid;		/* group id			*/
	__u32		bs_rdev;	/* device value			*/
	__s32		bs_blksize;	/* block size			*/
	__s64		bs_size;	/* file size			*/
	xfs_bstime_t	bs_atime;	/* access time			*/
	xfs_bstime_t	bs_mtime;	/* modify time			*/
	xfs_bstime_t	bs_ctime;	/* inode change time		*/
	int64_t		bs_blocks;	/* number of blocks		*/
	__u32		bs_xflags;	/* extended flags		*/
	__s32		bs_extsize;	/* extent size			*/
	__s32		bs_extents;	/* number of extents		*/
	__u32		bs_gen;		/* generation count		*/
	__u16		bs_projid;	/* project id			*/
	unsigned char	bs_pad[14];	/* pad space, unused		*/
	__u32		bs_dmevmask;	/* DMIG event mask		*/
	__u16		bs_dmstate;	/* DMIG state info		*/
	__u16		bs_aextents;	/* attribute number of extents	*/
} xfs_bstat_t;

/*
 * The user-level BulkStat Request interface structure.
 */
typedef struct xfs_fsop_bulkreq {
	__u64		__user *lastip;	/* last inode # pointer		*/
	__s32		icount;		/* count of entries in buffer	*/
	void		__user *ubuffer;/* user buffer for inode desc.	*/
	__s32		__user *ocount;	/* output count pointer		*/
} xfs_fsop_bulkreq_t;


/*
 * Structures returned from xfs_inumbers routine (XFS_IOC_FSINUMBERS).
 */
typedef struct xfs_inogrp {
	__u64		xi_startino;	/* starting inode number	*/
	__s32		xi_alloccount;	/* # bits set in allocmask	*/
	__u64		xi_allocmask;	/* mask of allocated inodes	*/
} xfs_inogrp_t;


/*
 * Error injection.
 */
typedef struct xfs_error_injection {
	__s32		fd;
	__s32		errtag;
} xfs_error_injection_t;


/*
 * The user-level Handle Request interface structure.
 */
typedef struct xfs_fsop_handlereq {
	__u32		fd;		/* fd for FD_TO_HANDLE		*/
	void		__user *path;	/* user pathname		*/
	__u32		oflags;		/* open flags			*/
	void		__user *ihandle;/* user supplied handle		*/
	__u32		ihandlen;	/* user supplied length		*/
	void		__user *ohandle;/* user buffer for handle	*/
	__u32		__user *ohandlen;/* user buffer length		*/
} xfs_fsop_handlereq_t;

/*
 * Compound structures for passing args through Handle Request interfaces
 * xfs_fssetdm_by_handle, xfs_attrlist_by_handle, xfs_attrmulti_by_handle
 * - ioctls: XFS_IOC_FSSETDM_BY_HANDLE, XFS_IOC_ATTRLIST_BY_HANDLE, and
 *	     XFS_IOC_ATTRMULTI_BY_HANDLE
 */

typedef struct xfs_fsop_setdm_handlereq {
	struct xfs_fsop_handlereq	hreq;	/* handle information	*/
	struct fsdmidata		__user *data;	/* DMAPI data	*/
} xfs_fsop_setdm_handlereq_t;

typedef struct xfs_attrlist_cursor {
	__u32		opaque[4];
} xfs_attrlist_cursor_t;

typedef struct xfs_fsop_attrlist_handlereq {
	struct xfs_fsop_handlereq	hreq; /* handle interface structure */
	struct xfs_attrlist_cursor	pos; /* opaque cookie, list offset */
	__u32				flags;	/* which namespace to use */
	__u32				buflen;	/* length of buffer supplied */
	void				__user *buffer;	/* returned names */
} xfs_fsop_attrlist_handlereq_t;

typedef struct xfs_attr_multiop {
	__u32		am_opcode;
#define ATTR_OP_GET	1	/* return the indicated attr's value */
#define ATTR_OP_SET	2	/* set/create the indicated attr/value pair */
#define ATTR_OP_REMOVE	3	/* remove the indicated attr */
	__s32		am_error;
	void		__user *am_attrname;
	void		__user *am_attrvalue;
	__u32		am_length;
	__u32		am_flags;
} xfs_attr_multiop_t;

typedef struct xfs_fsop_attrmulti_handlereq {
	struct xfs_fsop_handlereq	hreq; /* handle interface structure */
	__u32				opcount;/* count of following multiop */
	struct xfs_attr_multiop		__user *ops; /* attr_multi data */
} xfs_fsop_attrmulti_handlereq_t;

/*
 * per machine unique filesystem identifier types.
 */
typedef struct { __u32 val[2]; } xfs_fsid_t; /* file system id type */

typedef struct xfs_fid {
	__u16	fid_len;		/* length of remainder	*/
	__u16	fid_pad;
	__u32	fid_gen;		/* generation number	*/
	__u64	fid_ino;		/* 64 bits inode number */
} xfs_fid_t;

typedef struct xfs_handle {
	union {
		__s64	    align;	/* force alignment of ha_fid	 */
		xfs_fsid_t  _ha_fsid;	/* unique file system identifier */
	} ha_u;
	xfs_fid_t	ha_fid;		/* file system specific file ID	 */
} xfs_handle_t;
#define ha_fsid ha_u._ha_fsid

#define XFS_HSIZE(handle)	(((char *) &(handle).ha_fid.fid_pad	 \
				 - (char *) &(handle))			  \
				 + (handle).ha_fid.fid_len)

/*
 * Flags for going down operation
 */
#define XFS_FSOP_GOING_FLAGS_DEFAULT		0x0	/* going down */
#define XFS_FSOP_GOING_FLAGS_LOGFLUSH		0x1	/* flush log but not data */
#define XFS_FSOP_GOING_FLAGS_NOLOGFLUSH		0x2	/* don't flush log nor data */

/*
 * ioctl commands that are used by Linux filesystems
 */
#define XFS_IOC_GETXFLAGS	FS_IOC_GETFLAGS
#define XFS_IOC_SETXFLAGS	FS_IOC_SETFLAGS
#define XFS_IOC_GETVERSION	FS_IOC_GETVERSION
/* 32-bit compat counterparts */
#define XFS_IOC32_GETXFLAGS	FS_IOC32_GETFLAGS
#define XFS_IOC32_SETXFLAGS	FS_IOC32_SETFLAGS
#define XFS_IOC32_GETVERSION	FS_IOC32_GETVERSION

/*
 * ioctl commands that replace IRIX fcntl()'s
 * For 'documentation' purposed more than anything else,
 * the "cmd #" field reflects the IRIX fcntl number.
 */
#define XFS_IOC_ALLOCSP		_IOW ('X', 10, struct xfs_flock64)
#define XFS_IOC_FREESP		_IOW ('X', 11, struct xfs_flock64)
#define XFS_IOC_DIOINFO		_IOR ('X', 30, struct dioattr)
#define XFS_IOC_FSGETXATTR	_IOR ('X', 31, struct fsxattr)
#define XFS_IOC_FSSETXATTR	_IOW ('X', 32, struct fsxattr)
#define XFS_IOC_ALLOCSP64	_IOW ('X', 36, struct xfs_flock64)
#define XFS_IOC_FREESP64	_IOW ('X', 37, struct xfs_flock64)
#define XFS_IOC_GETBMAP		_IOWR('X', 38, struct getbmap)
#define XFS_IOC_FSSETDM		_IOW ('X', 39, struct fsdmidata)
#define XFS_IOC_RESVSP		_IOW ('X', 40, struct xfs_flock64)
#define XFS_IOC_UNRESVSP	_IOW ('X', 41, struct xfs_flock64)
#define XFS_IOC_RESVSP64	_IOW ('X', 42, struct xfs_flock64)
#define XFS_IOC_UNRESVSP64	_IOW ('X', 43, struct xfs_flock64)
#define XFS_IOC_GETBMAPA	_IOWR('X', 44, struct getbmap)
#define XFS_IOC_FSGETXATTRA	_IOR ('X', 45, struct fsxattr)
/*	XFS_IOC_SETBIOSIZE ---- deprecated 46	   */
/*	XFS_IOC_GETBIOSIZE ---- deprecated 47	   */
#define XFS_IOC_GETBMAPX	_IOWR('X', 56, struct getbmap)

/*
 * ioctl commands that replace IRIX syssgi()'s
 */
#define XFS_IOC_FSGEOMETRY_V1	     _IOR ('X', 100, struct xfs_fsop_geom_v1)
#define XFS_IOC_FSBULKSTAT	     _IOWR('X', 101, struct xfs_fsop_bulkreq)
#define XFS_IOC_FSBULKSTAT_SINGLE    _IOWR('X', 102, struct xfs_fsop_bulkreq)
#define XFS_IOC_FSINUMBERS	     _IOWR('X', 103, struct xfs_fsop_bulkreq)
#define XFS_IOC_PATH_TO_FSHANDLE     _IOWR('X', 104, struct xfs_fsop_handlereq)
#define XFS_IOC_PATH_TO_HANDLE	     _IOWR('X', 105, struct xfs_fsop_handlereq)
#define XFS_IOC_FD_TO_HANDLE	     _IOWR('X', 106, struct xfs_fsop_handlereq)
#define XFS_IOC_OPEN_BY_HANDLE	     _IOWR('X', 107, struct xfs_fsop_handlereq)
#define XFS_IOC_READLINK_BY_HANDLE   _IOWR('X', 108, struct xfs_fsop_handlereq)
#define XFS_IOC_SWAPEXT		     _IOWR('X', 109, struct xfs_swapext)
#define XFS_IOC_FSGROWFSDATA	     _IOW ('X', 110, struct xfs_growfs_data)
#define XFS_IOC_FSGROWFSLOG	     _IOW ('X', 111, struct xfs_growfs_log)
#define XFS_IOC_FSGROWFSRT	     _IOW ('X', 112, struct xfs_growfs_rt)
#define XFS_IOC_FSCOUNTS	     _IOR ('X', 113, struct xfs_fsop_counts)
#define XFS_IOC_SET_RESBLKS	     _IOWR('X', 114, struct xfs_fsop_resblks)
#define XFS_IOC_GET_RESBLKS	     _IOR ('X', 115, struct xfs_fsop_resblks)
#define XFS_IOC_ERROR_INJECTION	     _IOW ('X', 116, struct xfs_error_injection)
#define XFS_IOC_ERROR_CLEARALL	     _IOW ('X', 117, struct xfs_error_injection)
/*	XFS_IOC_ATTRCTL_BY_HANDLE -- deprecated 118	 */
#define XFS_IOC_FREEZE		     _IOWR('X', 119, int)
#define XFS_IOC_THAW		     _IOWR('X', 120, int)
#define XFS_IOC_FSSETDM_BY_HANDLE    _IOW ('X', 121, struct xfs_fsop_setdm_handlereq)
#define XFS_IOC_ATTRLIST_BY_HANDLE   _IOW ('X', 122, struct xfs_fsop_attrlist_handlereq)
#define XFS_IOC_ATTRMULTI_BY_HANDLE  _IOW ('X', 123, struct xfs_fsop_attrmulti_handlereq)
#define XFS_IOC_FSGEOMETRY	     _IOR ('X', 124, struct xfs_fsop_geom)
#define XFS_IOC_GOINGDOWN	     _IOR ('X', 125, __uint32_t)
/*	XFS_IOC_GETFSUUID ---------- deprecated 140	 */


#ifndef HAVE_BBMACROS
/*
 * Block I/O parameterization.	A basic block (BB) is the lowest size of
 * filesystem allocation, and must equal 512.  Length units given to bio
 * routines are in BB's.
 */
#define BBSHIFT		9
#define BBSIZE		(1<<BBSHIFT)
#define BBMASK		(BBSIZE-1)
#define BTOBB(bytes)	(((__u64)(bytes) + BBSIZE - 1) >> BBSHIFT)
#define BTOBBT(bytes)	((__u64)(bytes) >> BBSHIFT)
#define BBTOB(bbs)	((bbs) << BBSHIFT)
#endif

#endif	/* __XFS_FS_H__ */
