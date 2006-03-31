#ifndef _LINUX_IFLAGS_H
#define _LINUX_IFLAGS_H

/*
 * A universal set of inode flags.
 *
 * Originally taken from ext2/3 with additions for other filesystems.
 * Filesystems supporting this interface should interoperate with
 * the lsattr and chattr command line tools.
 *
 * This interface is supported in whole or in part by:
 * ext2
 * ext3
 * xfs
 * jfs
 * gfs2
 *
 */

#define IFLAGS_GET_IOC		_IOR('f', 1, long)
#define IFLAGS_SET_IOC		_IOW('f', 2, long)

/*
 * These values are provided for use as indices of an array
 * for use with the iflags_cvt function below
 */
enum {
	iflag_SecureRm		= 0,	/* Secure deletion */
	iflag_Unrm		= 1,	/* Undelete */
	iflag_Compress		= 2,	/* Compress file */
	iflag_Sync		= 3,	/* Synchronous updates */
	iflag_Immutable	= 4,	/* Immutable */
	iflag_Append		= 5,	/* Append */
	iflag_NoDump		= 6,	/* Don't dump file */
	iflag_NoAtime		= 7,	/* No atime updates */
	/* Reserved for compression usage */
	iflag_Dirty		= 8,
	iflag_ComprBlk		= 9,	/* One or more compressed clusters */
	iflag_NoComp		= 10,	/* Don't compress */
	iflag_Ecompr		= 11,	/* Compression error */
	/* End of compression flags */
	iflag_Btree		= 12,	/* btree format dir */
	iflag_Index		= 12,	/* hash-indexed directory */
	iflag_Imagic		= 13,	/* AFS directory */
	iflag_JournalData	= 14,	/* file data should be journaled */
	iflag_NoTail		= 15,	/* file tail should not be merged */
	iflag_DirSync		= 16,	/* dirsync behaviour */
	iflag_TopDir		= 17,	/* Top of directory hierarchies */
	iflag_DirectIO		= 18,	/* Always use direct I/O on this file */
	iflag_InheritDirectIO	= 19,	/* Set DirectIO on new files in dir */
	iflag_InheritJdata	= 20,	/* Set JournalData on create in dir */
	iflag_Reserved		= 31	/* reserved for ext2/3 lib */
};

#define __IFL(x) (1<<(iflag_##x))
#define IFLAG_SECRM		__IFL(SecureRm)		/* 0x00000001 */
#define IFLAG_UNRM		__IFL(Unrm)		/* 0x00000002 */
#define IFLAG_COMPR		__IFL(Compr)		/* 0x00000004 */
#define IFLAG_SYNC		__IFL(Sync)		/* 0x00000008 */
#define IFLAG_IMMUTABLE		__IFL(Immutable)	/* 0x00000010 */
#define IFLAG_APPEND		__IFL(Append)		/* 0x00000020 */
#define IFLAG_NODUMP		__IFL(NoDump)		/* 0x00000040 */
#define IFLAG_NOATIME		__IFL(NoAtime)		/* 0x00000080 */
#define IFLAG_DIRTY		__IFL(Dirty)		/* 0x00000100 */
#define IFLAG_COMPRBLK		__IFL(ComprBlk)		/* 0x00000200 */
#define IFLAG_NOCOMP		__IFL(NoComp)		/* 0x00000400 */
#define IFLAG_ECOMPR		__IFL(Ecompr)		/* 0x00000800 */
#define IFLAG_BTREE		__IFL(Btree)		/* 0x00001000 */
#define IFLAG_INDEX		__IFL(Index)		/* 0x00001000 */
#define IFLAG_IMAGIC		__IFL(Imagic)		/* 0x00002000 */
#define IFLAG_JOURNAL_DATA	__IFL(JournalData)	/* 0x00004000 */
#define IFLAG_NOTAIL		__IFL(NoTail)		/* 0x00008000 */
#define IFLAG_DIRSYNC		__IFL(DirSync)		/* 0x00010000 */
#define IFLAG_TOPDIR		__IFL(TopDir)		/* 0x00020000 */
#define IFLAG_DIRECTIO		__IFL(DirectIO)		/* 0x00040000 */
#define IFLAG_INHERITDIRECTIO	__IFL(InheritDirectIO)	/* 0x00080000 */
#define IFLAG_INHERITJDATA	__IFL(InheritJdata)	/* 0x00100000 */
#define IFLAG_RESERVED		__IFL(Reserved)		/* 0x80000000 */

#ifdef __KERNEL__
/**
 * iflags_cvt
 * @table: A table of 32 u32 flags
 * @val: a 32 bit value to convert
 *
 * This function can be used to convert between IFLAGS values and
 * the filesystem's own flags values.
 *
 * Returns: the converted flags
 */
static inline u32 iflags_cvt(const u32 *table, u32 val)
{
	u32 res = 0;
	while(val) {
		if (val & 1)
			res |= *table;
		table++;
		val >>= 1;
	}
	return res;
}
#endif /* __KERNEL__ */

#endif /* _LINUX_IFLAGS_H */
