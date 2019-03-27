/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Aditya Sarawgi
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _FS_EXT2FS_EXT2_DIR_H_
#define	_FS_EXT2FS_EXT2_DIR_H_

/*
 * Structure of a directory entry
 */
#define	EXT2FS_MAXNAMLEN	255

struct ext2fs_direct {
	uint32_t e2d_ino;		/* inode number of entry */
	uint16_t e2d_reclen;		/* length of this record */
	uint16_t e2d_namlen;		/* length of string in e2d_name */
	char e2d_name[EXT2FS_MAXNAMLEN];/* name with length<=EXT2FS_MAXNAMLEN */
};

enum slotstatus {
	NONE,
	COMPACT,
	FOUND
};

struct ext2fs_searchslot {
	enum slotstatus slotstatus;
	doff_t	slotoffset;		/* offset of area with free space */
	int	slotsize;		/* size of area at slotoffset */
	int	slotfreespace;		/* amount of space free in slot */
	int	slotneeded;		/* sizeof the entry we are seeking */
};

/*
 * The new version of the directory entry.  Since EXT2 structures are
 * stored in intel byte order, and the name_len field could never be
 * bigger than 255 chars, it's safe to reclaim the extra byte for the
 * file_type field.
 */
struct ext2fs_direct_2 {
	uint32_t e2d_ino;		/* inode number of entry */
	uint16_t e2d_reclen;		/* length of this record */
	uint8_t	e2d_namlen;		/* length of string in e2d_name */
	uint8_t	e2d_type;		/* file type */
	char	e2d_name[EXT2FS_MAXNAMLEN];	/* name with
						 * length<=EXT2FS_MAXNAMLEN */
};

struct ext2fs_direct_tail {
	uint32_t e2dt_reserved_zero1;	/* pretend to be unused */
	uint16_t e2dt_rec_len;		/* 12 */
	uint8_t	e2dt_reserved_zero2;	/* zero name length */
	uint8_t	e2dt_reserved_ft;	/* 0xDE, fake file type */
	uint32_t e2dt_checksum;		/* crc32c(uuid+inum+dirblock) */
};

#define EXT2_FT_DIR_CSUM	0xDE

#define EXT2_DIRENT_TAIL(data, blocksize) \
	((struct ext2fs_direct_tail *)(((char *)(data)) + \
	(blocksize) - sizeof(struct ext2fs_direct_tail)))

/*
 * Maximal count of links to a file
 */
#define	EXT4_LINK_MAX	65000

/*
 * Ext2 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
#define	EXT2_FT_UNKNOWN		0
#define	EXT2_FT_REG_FILE	1
#define	EXT2_FT_DIR		2
#define	EXT2_FT_CHRDEV		3
#define	EXT2_FT_BLKDEV 		4
#define	EXT2_FT_FIFO		5
#define	EXT2_FT_SOCK		6
#define	EXT2_FT_SYMLINK		7
#define	EXT2_FT_MAX		8

/*
 * EXT2_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define	EXT2_DIR_PAD		 	4
#define	EXT2_DIR_ROUND			(EXT2_DIR_PAD - 1)
#define	EXT2_DIR_REC_LEN(name_len)	(((name_len) + 8 + EXT2_DIR_ROUND) & \
					 ~EXT2_DIR_ROUND)
#endif	/* !_FS_EXT2FS_EXT2_DIR_H_ */
