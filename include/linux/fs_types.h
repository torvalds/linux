/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_TYPES_H
#define _LINUX_FS_TYPES_H

/*
 * This is a header for the common implementation of dirent
 * to fs on-disk file type conversion.  Although the fs on-disk
 * bits are specific to every file system, in practice, many
 * file systems use the exact same on-disk format to describe
 * the lower 3 file type bits that represent the 7 POSIX file
 * types.
 *
 * It is important to note that the definitions in this
 * header MUST NOT change. This would break both the
 * userspace ABI and the on-disk format of filesystems
 * using this code.
 *
 * All those file systems can use this generic code for the
 * conversions.
 */

/*
 * struct dirent file types
 * exposed to user via getdents(2), readdir(3)
 *
 * These match bits 12..15 of stat.st_mode
 * (ie "(i_mode >> 12) & 15").
 */
#define S_DT_SHIFT	12
#define S_DT(mode)	(((mode) & S_IFMT) >> S_DT_SHIFT)
#define S_DT_MASK	(S_IFMT >> S_DT_SHIFT)

/* these are defined by POSIX and also present in glibc's dirent.h */
#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14

#define DT_MAX		(S_DT_MASK + 1) /* 16 */

/*
 * fs on-disk file types.
 * Only the low 3 bits are used for the POSIX file types.
 * Other bits are reserved for fs private use.
 * These definitions are shared and used by multiple filesystems,
 * and MUST NOT change under any circumstances.
 *
 * Note that no fs currently stores the whiteout type on-disk,
 * so whiteout dirents are exposed to user as DT_CHR.
 */
#define FT_UNKNOWN	0
#define FT_REG_FILE	1
#define FT_DIR		2
#define FT_CHRDEV	3
#define FT_BLKDEV	4
#define FT_FIFO		5
#define FT_SOCK		6
#define FT_SYMLINK	7

#define FT_MAX		8

/*
 * declarations for helper functions, accompanying implementation
 * is in fs/fs_types.c
 */
extern unsigned char fs_ftype_to_dtype(unsigned int filetype);
extern unsigned char fs_umode_to_ftype(umode_t mode);
extern unsigned char fs_umode_to_dtype(umode_t mode);

#endif
