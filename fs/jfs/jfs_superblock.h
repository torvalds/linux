/*
 *   Copyright (C) International Business Machines Corp., 2000-2003
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef	_H_JFS_SUPERBLOCK
#define _H_JFS_SUPERBLOCK

/*
 * make the magic number something a human could read
 */
#define JFS_MAGIC 	"JFS1"	/* Magic word */

#define JFS_VERSION	2	/* Version number: Version 2 */

#define LV_NAME_SIZE	11	/* MUST BE 11 for OS/2 boot sector */

/* 
 *	aggregate superblock 
 *
 * The name superblock is too close to super_block, so the name has been
 * changed to jfs_superblock.  The utilities are still using the old name.
 */
struct jfs_superblock {
	char s_magic[4];	/* 4: magic number */
	__le32 s_version;	/* 4: version number */

	__le64 s_size;		/* 8: aggregate size in hardware/LVM blocks;
				 * VFS: number of blocks
				 */
	__le32 s_bsize;		/* 4: aggregate block size in bytes; 
				 * VFS: fragment size
				 */
	__le16 s_l2bsize;	/* 2: log2 of s_bsize */
	__le16 s_l2bfactor;	/* 2: log2(s_bsize/hardware block size) */
	__le32 s_pbsize;	/* 4: hardware/LVM block size in bytes */
	__le16 s_l2pbsize;	/* 2: log2 of s_pbsize */
	__le16 pad;		/* 2: padding necessary for alignment */

	__le32 s_agsize;	/* 4: allocation group size in aggr. blocks */

	__le32 s_flag;		/* 4: aggregate attributes:
				 *    see jfs_filsys.h
				 */
	__le32 s_state;		/* 4: mount/unmount/recovery state: 
				 *    see jfs_filsys.h
				 */
	__le32 s_compress;		/* 4: > 0 if data compression */

	pxd_t s_ait2;		/* 8: first extent of secondary
				 *    aggregate inode table
				 */

	pxd_t s_aim2;		/* 8: first extent of secondary
				 *    aggregate inode map
				 */
	__le32 s_logdev;		/* 4: device address of log */
	__le32 s_logserial;	/* 4: log serial number at aggregate mount */
	pxd_t s_logpxd;		/* 8: inline log extent */

	pxd_t s_fsckpxd;	/* 8: inline fsck work space extent */

	struct timestruc_t s_time;	/* 8: time last updated */

	__le32 s_fsckloglen;	/* 4: Number of filesystem blocks reserved for
				 *    the fsck service log.  
				 *    N.B. These blocks are divided among the
				 *         versions kept.  This is not a per
				 *         version size.
				 *    N.B. These blocks are included in the 
				 *         length field of s_fsckpxd.
				 */
	s8 s_fscklog;		/* 1: which fsck service log is most recent
				 *    0 => no service log data yet
				 *    1 => the first one
				 *    2 => the 2nd one
				 */
	char s_fpack[11];	/* 11: file system volume name 
				 *     N.B. This must be 11 bytes to
				 *          conform with the OS/2 BootSector
				 *          requirements
				 *          Only used when s_version is 1
				 */

	/* extendfs() parameter under s_state & FM_EXTENDFS */
	__le64 s_xsize;		/* 8: extendfs s_size */
	pxd_t s_xfsckpxd;	/* 8: extendfs fsckpxd */
	pxd_t s_xlogpxd;	/* 8: extendfs logpxd */
	/* - 128 byte boundary - */

	char s_uuid[16];	/* 16: 128-bit uuid for volume */
	char s_label[16];	/* 16: volume label */
	char s_loguuid[16];	/* 16: 128-bit uuid for log device */

};

extern int readSuper(struct super_block *, struct buffer_head **);
extern int updateSuper(struct super_block *, uint);
extern void jfs_error(struct super_block *, const char *, ...);
extern int jfs_mount(struct super_block *);
extern int jfs_mount_rw(struct super_block *, int);
extern int jfs_umount(struct super_block *);
extern int jfs_umount_rw(struct super_block *);
extern int jfs_extendfs(struct super_block *, s64, int);

extern struct task_struct *jfsIOthread;
extern struct task_struct *jfsSyncThread;

#endif /*_H_JFS_SUPERBLOCK */
