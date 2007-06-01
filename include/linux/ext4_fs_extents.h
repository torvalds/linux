/*
 * Copyright (c) 2003-2006, Cluster File Systems, Inc, info@clusterfs.com
 * Written by Alex Tomas <alex@clusterfs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 */

#ifndef _LINUX_EXT4_EXTENTS
#define _LINUX_EXT4_EXTENTS

#include <linux/ext4_fs.h>

/*
 * With AGGRESSIVE_TEST defined, the capacity of index/leaf blocks
 * becomes very small, so index split, in-depth growing and
 * other hard changes happen much more often.
 * This is for debug purposes only.
 */
#define AGGRESSIVE_TEST_

/*
 * With EXTENTS_STATS defined, the number of blocks and extents
 * are collected in the truncate path. They'll be shown at
 * umount time.
 */
#define EXTENTS_STATS__

/*
 * If CHECK_BINSEARCH is defined, then the results of the binary search
 * will also be checked by linear search.
 */
#define CHECK_BINSEARCH__

/*
 * If EXT_DEBUG is defined you can use the 'extdebug' mount option
 * to get lots of info about what's going on.
 */
#define EXT_DEBUG__
#ifdef EXT_DEBUG
#define ext_debug(a...)		printk(a)
#else
#define ext_debug(a...)
#endif

/*
 * If EXT_STATS is defined then stats numbers are collected.
 * These number will be displayed at umount time.
 */
#define EXT_STATS_


/*
 * ext4_inode has i_block array (60 bytes total).
 * The first 12 bytes store ext4_extent_header;
 * the remainder stores an array of ext4_extent.
 */

/*
 * This is the extent on-disk structure.
 * It's used at the bottom of the tree.
 */
struct ext4_extent {
	__le32	ee_block;	/* first logical block extent covers */
	__le16	ee_len;		/* number of blocks covered by extent */
	__le16	ee_start_hi;	/* high 16 bits of physical block */
	__le32	ee_start;	/* low 32 bits of physical block */
};

/*
 * This is index on-disk structure.
 * It's used at all the levels except the bottom.
 */
struct ext4_extent_idx {
	__le32	ei_block;	/* index covers logical blocks from 'block' */
	__le32	ei_leaf;	/* pointer to the physical block of the next *
				 * level. leaf or next index could be there */
	__le16	ei_leaf_hi;	/* high 16 bits of physical block */
	__u16	ei_unused;
};

/*
 * Each block (leaves and indexes), even inode-stored has header.
 */
struct ext4_extent_header {
	__le16	eh_magic;	/* probably will support different formats */
	__le16	eh_entries;	/* number of valid entries */
	__le16	eh_max;		/* capacity of store in entries */
	__le16	eh_depth;	/* has tree real underlying blocks? */
	__le32	eh_generation;	/* generation of the tree */
};

#define EXT4_EXT_MAGIC		cpu_to_le16(0xf30a)

/*
 * Array of ext4_ext_path contains path to some extent.
 * Creation/lookup routines use it for traversal/splitting/etc.
 * Truncate uses it to simulate recursive walking.
 */
struct ext4_ext_path {
	ext4_fsblk_t			p_block;
	__u16				p_depth;
	struct ext4_extent		*p_ext;
	struct ext4_extent_idx		*p_idx;
	struct ext4_extent_header	*p_hdr;
	struct buffer_head		*p_bh;
};

/*
 * structure for external API
 */

#define EXT4_EXT_CACHE_NO	0
#define EXT4_EXT_CACHE_GAP	1
#define EXT4_EXT_CACHE_EXTENT	2

/*
 * to be called by ext4_ext_walk_space()
 * negative retcode - error
 * positive retcode - signal for ext4_ext_walk_space(), see below
 * callback must return valid extent (passed or newly created)
 */
typedef int (*ext_prepare_callback)(struct inode *, struct ext4_ext_path *,
					struct ext4_ext_cache *,
					void *);

#define EXT_CONTINUE	0
#define EXT_BREAK	1
#define EXT_REPEAT	2


#define EXT_MAX_BLOCK	0xffffffff

#define EXT_MAX_LEN	((1UL << 15) - 1)


#define EXT_FIRST_EXTENT(__hdr__) \
	((struct ext4_extent *) (((char *) (__hdr__)) +		\
				 sizeof(struct ext4_extent_header)))
#define EXT_FIRST_INDEX(__hdr__) \
	((struct ext4_extent_idx *) (((char *) (__hdr__)) +	\
				     sizeof(struct ext4_extent_header)))
#define EXT_HAS_FREE_INDEX(__path__) \
	(le16_to_cpu((__path__)->p_hdr->eh_entries) \
				     < le16_to_cpu((__path__)->p_hdr->eh_max))
#define EXT_LAST_EXTENT(__hdr__) \
	(EXT_FIRST_EXTENT((__hdr__)) + le16_to_cpu((__hdr__)->eh_entries) - 1)
#define EXT_LAST_INDEX(__hdr__) \
	(EXT_FIRST_INDEX((__hdr__)) + le16_to_cpu((__hdr__)->eh_entries) - 1)
#define EXT_MAX_EXTENT(__hdr__) \
	(EXT_FIRST_EXTENT((__hdr__)) + le16_to_cpu((__hdr__)->eh_max) - 1)
#define EXT_MAX_INDEX(__hdr__) \
	(EXT_FIRST_INDEX((__hdr__)) + le16_to_cpu((__hdr__)->eh_max) - 1)

static inline struct ext4_extent_header *ext_inode_hdr(struct inode *inode)
{
	return (struct ext4_extent_header *) EXT4_I(inode)->i_data;
}

static inline struct ext4_extent_header *ext_block_hdr(struct buffer_head *bh)
{
	return (struct ext4_extent_header *) bh->b_data;
}

static inline unsigned short ext_depth(struct inode *inode)
{
	return le16_to_cpu(ext_inode_hdr(inode)->eh_depth);
}

static inline void ext4_ext_tree_changed(struct inode *inode)
{
	EXT4_I(inode)->i_ext_generation++;
}

static inline void
ext4_ext_invalidate_cache(struct inode *inode)
{
	EXT4_I(inode)->i_cached_extent.ec_type = EXT4_EXT_CACHE_NO;
}

extern int ext4_extent_tree_init(handle_t *, struct inode *);
extern int ext4_ext_calc_credits_for_insert(struct inode *, struct ext4_ext_path *);
extern unsigned int ext4_ext_check_overlap(struct inode *, struct ext4_extent *, struct ext4_ext_path *);
extern int ext4_ext_insert_extent(handle_t *, struct inode *, struct ext4_ext_path *, struct ext4_extent *);
extern int ext4_ext_walk_space(struct inode *, unsigned long, unsigned long, ext_prepare_callback, void *);
extern struct ext4_ext_path * ext4_ext_find_extent(struct inode *, int, struct ext4_ext_path *);

#endif /* _LINUX_EXT4_EXTENTS */

