/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2010 Zheng Liu <lz@freebsd.org>
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
#ifndef _FS_EXT2FS_EXT2_EXTENTS_H_
#define	_FS_EXT2FS_EXT2_EXTENTS_H_

#include <sys/types.h>

#define	EXT4_EXT_MAGIC  0xf30a
#define EXT4_MAX_BLOCKS 0xffffffff
#define EXT_INIT_MAX_LEN (1UL << 15)
#define EXT4_MAX_LEN	(EXT_INIT_MAX_LEN - 1)
#define EXT4_EXT_DEPTH_MAX 5

#define	EXT4_EXT_CACHE_NO	0
#define	EXT4_EXT_CACHE_GAP	1
#define	EXT4_EXT_CACHE_IN	2

/*
 * Ext4 extent tail with csum
 */
struct ext4_extent_tail {
	uint32_t et_checksum;	/* crc32c(uuid+inum+extent_block) */
};

/*
 * Ext4 file system extent on disk.
 */
struct ext4_extent {
	uint32_t e_blk;			/* first logical block */
	uint16_t e_len;			/* number of blocks */
	uint16_t e_start_hi;		/* high 16 bits of physical block */
	uint32_t e_start_lo;		/* low 32 bits of physical block */
};

/*
 * Extent index on disk.
 */
struct ext4_extent_index {
	uint32_t ei_blk;	/* indexes logical blocks */
	uint32_t ei_leaf_lo;	/* points to physical block of the
				 * next level */
	uint16_t ei_leaf_hi;	/* high 16 bits of physical block */
	uint16_t ei_unused;
};

/*
 * Extent tree header.
 */
struct ext4_extent_header {
	uint16_t eh_magic;		/* magic number: 0xf30a */
	uint16_t eh_ecount;		/* number of valid entries */
	uint16_t eh_max;		/* capacity of store in entries */
	uint16_t eh_depth;		/* the depth of extent tree */
	uint32_t eh_gen;		/* generation of extent tree */
};

/*
 * Save cached extent.
 */
struct ext4_extent_cache {
	daddr_t	ec_start;		/* extent start */
	uint32_t ec_blk;		/* logical block */
	uint32_t ec_len;
	uint32_t ec_type;
};

/*
 * Save path to some extent.
 */
struct ext4_extent_path {
	int index_count;
	uint16_t ep_depth;
	uint64_t ep_blk;
	char *ep_data;
	struct ext4_extent *ep_ext;
	struct ext4_extent_index *ep_index;
	struct ext4_extent_header *ep_header;
};

#define EXT_FIRST_EXTENT(hdr) ((struct ext4_extent *)(((char *)(hdr)) + \
    sizeof(struct ext4_extent_header)))
#define EXT_FIRST_INDEX(hdr) ((struct ext4_extent_index *)(((char *)(hdr)) + \
    sizeof(struct ext4_extent_header)))
#define EXT_LAST_EXTENT(hdr) (EXT_FIRST_EXTENT((hdr)) + (hdr)->eh_ecount - 1)
#define EXT_LAST_INDEX(hdr) (EXT_FIRST_INDEX((hdr)) + (hdr)->eh_ecount - 1)
#define EXT4_EXTENT_TAIL_OFFSET(hdr) (sizeof(struct ext4_extent_header) + \
    (sizeof(struct ext4_extent) * (hdr)->eh_max))
#define EXT_HAS_FREE_INDEX(path) \
    ((path)->ep_header->eh_ecount < (path)->ep_header->eh_max)
#define EXT_MAX_EXTENT(hdr) (EXT_FIRST_EXTENT(hdr) + ((hdr)->eh_max) - 1)
#define EXT_MAX_INDEX(hdr) (EXT_FIRST_INDEX((hdr)) + (hdr)->eh_max - 1)

struct inode;
struct m_ext2fs;
void	ext4_ext_tree_init(struct inode *ip);
int	ext4_ext_in_cache(struct inode *, daddr_t, struct ext4_extent *);
void	ext4_ext_put_cache(struct inode *, struct ext4_extent *, int);
int ext4_ext_find_extent(struct inode *, daddr_t, struct ext4_extent_path **);
void ext4_ext_path_free(struct ext4_extent_path *path);
int ext4_ext_remove_space(struct inode *ip, off_t length, int flags,
    struct ucred *cred, struct thread *td);
int ext4_ext_get_blocks(struct inode *ip, int64_t iblock,
    unsigned long max_blocks, struct ucred *cred, struct buf **bpp,
    int *allocate, daddr_t *);
#ifdef EXT2FS_DEBUG
void ext4_ext_print_extent_tree_status(struct inode *ip);
#endif

#endif	/* !_FS_EXT2FS_EXT2_EXTENTS_H_ */
