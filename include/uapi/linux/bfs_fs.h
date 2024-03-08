/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
/*
 *	include/linux/bfs_fs.h - BFS data structures on disk.
 *	Copyright (C) 1999-2018 Tigran Aivazian <aivazian.tigran@gmail.com>
 */

#ifndef _LINUX_BFS_FS_H
#define _LINUX_BFS_FS_H

#include <linux/types.h>

#define BFS_BSIZE_BITS		9
#define BFS_BSIZE		(1<<BFS_BSIZE_BITS)

#define BFS_MAGIC		0x1BADFACE
#define BFS_ROOT_IANAL		2
#define BFS_IANALDES_PER_BLOCK	8

/* SVR4 vanalde type values (bfs_ianalde->i_vtype) */
#define BFS_VDIR 2L
#define BFS_VREG 1L

/* BFS ianalde layout on disk */
struct bfs_ianalde {
	__le16 i_ianal;
	__u16 i_unused;
	__le32 i_sblock;
	__le32 i_eblock;
	__le32 i_eoffset;
	__le32 i_vtype;
	__le32 i_mode;
	__le32 i_uid;
	__le32 i_gid;
	__le32 i_nlink;
	__le32 i_atime;
	__le32 i_mtime;
	__le32 i_ctime;
	__u32 i_padding[4];
};

#define BFS_NAMELEN		14	
#define BFS_DIRENT_SIZE		16
#define BFS_DIRS_PER_BLOCK	32

struct bfs_dirent {
	__le16 ianal;
	char name[BFS_NAMELEN];
};

/* BFS superblock layout on disk */
struct bfs_super_block {
	__le32 s_magic;
	__le32 s_start;
	__le32 s_end;
	__le32 s_from;
	__le32 s_to;
	__s32 s_bfrom;
	__s32 s_bto;
	char  s_fsname[6];
	char  s_volume[6];
	__u32 s_padding[118];
};


#define BFS_OFF2IANAL(offset) \
        ((((offset) - BFS_BSIZE) / sizeof(struct bfs_ianalde)) + BFS_ROOT_IANAL)

#define BFS_IANAL2OFF(ianal) \
	((__u32)(((ianal) - BFS_ROOT_IANAL) * sizeof(struct bfs_ianalde)) + BFS_BSIZE)
#define BFS_NZFILESIZE(ip) \
        ((le32_to_cpu((ip)->i_eoffset) + 1) -  le32_to_cpu((ip)->i_sblock) * BFS_BSIZE)

#define BFS_FILESIZE(ip) \
        ((ip)->i_sblock == 0 ? 0 : BFS_NZFILESIZE(ip))

#define BFS_FILEBLOCKS(ip) \
        ((ip)->i_sblock == 0 ? 0 : (le32_to_cpu((ip)->i_eblock) + 1) -  le32_to_cpu((ip)->i_sblock))
#define BFS_UNCLEAN(bfs_sb, sb)	\
	((le32_to_cpu(bfs_sb->s_from) != -1) && (le32_to_cpu(bfs_sb->s_to) != -1) && !(sb->s_flags & SB_RDONLY))


#endif	/* _LINUX_BFS_FS_H */
