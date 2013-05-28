/*
 * Copyright (c) 2000,2002-2003,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
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
#ifndef __XFS_ATTR_LEAF_H__
#define	__XFS_ATTR_LEAF_H__

/*
 * Attribute storage layout, internal structure, access macros, etc.
 *
 * Attribute lists are structured around Btrees where all the data
 * elements are in the leaf nodes.  Attribute names are hashed into an int,
 * then that int is used as the index into the Btree.  Since the hashval
 * of an attribute name may not be unique, we may have duplicate keys.  The
 * internal links in the Btree are logical block offsets into the file.
 */

struct attrlist;
struct attrlist_cursor_kern;
struct xfs_attr_list_context;
struct xfs_da_args;
struct xfs_da_state;
struct xfs_da_state_blk;
struct xfs_inode;
struct xfs_trans;

/*========================================================================
 * Attribute structure when equal to XFS_LBSIZE(mp) bytes.
 *========================================================================*/

/*
 * This is the structure of the leaf nodes in the Btree.
 *
 * Struct leaf_entry's are packed from the top.  Name/values grow from the
 * bottom but are not packed.  The freemap contains run-length-encoded entries
 * for the free bytes after the leaf_entry's, but only the N largest such,
 * smaller runs are dropped.  When the freemap doesn't show enough space
 * for an allocation, we compact the name/value area and try again.  If we
 * still don't have enough space, then we have to split the block.  The
 * name/value structs (both local and remote versions) must be 32bit aligned.
 *
 * Since we have duplicate hash keys, for each key that matches, compare
 * the actual name string.  The root and intermediate node search always
 * takes the first-in-the-block key match found, so we should only have
 * to work "forw"ard.  If none matches, continue with the "forw"ard leaf
 * nodes until the hash key changes or the attribute name is found.
 *
 * We store the fact that an attribute is a ROOT/USER/SECURE attribute in
 * the leaf_entry.  The namespaces are independent only because we also look
 * at the namespace bit when we are looking for a matching attribute name.
 *
 * We also store an "incomplete" bit in the leaf_entry.  It shows that an
 * attribute is in the middle of being created and should not be shown to
 * the user if we crash during the time that the bit is set.  We clear the
 * bit when we have finished setting up the attribute.  We do this because
 * we cannot create some large attributes inside a single transaction, and we
 * need some indication that we weren't finished if we crash in the middle.
 */
#define XFS_ATTR_LEAF_MAPSIZE	3	/* how many freespace slots */

typedef struct xfs_attr_leaf_map {	/* RLE map of free bytes */
	__be16	base;			  /* base of free region */
	__be16	size;			  /* length of free region */
} xfs_attr_leaf_map_t;

typedef struct xfs_attr_leaf_hdr {	/* constant-structure header block */
	xfs_da_blkinfo_t info;		/* block type, links, etc. */
	__be16	count;			/* count of active leaf_entry's */
	__be16	usedbytes;		/* num bytes of names/values stored */
	__be16	firstused;		/* first used byte in name area */
	__u8	holes;			/* != 0 if blk needs compaction */
	__u8	pad1;
	xfs_attr_leaf_map_t freemap[XFS_ATTR_LEAF_MAPSIZE];
					/* N largest free regions */
} xfs_attr_leaf_hdr_t;

typedef struct xfs_attr_leaf_entry {	/* sorted on key, not name */
	__be32	hashval;		/* hash value of name */
	__be16	nameidx;		/* index into buffer of name/value */
	__u8	flags;			/* LOCAL/ROOT/SECURE/INCOMPLETE flag */
	__u8	pad2;			/* unused pad byte */
} xfs_attr_leaf_entry_t;

typedef struct xfs_attr_leaf_name_local {
	__be16	valuelen;		/* number of bytes in value */
	__u8	namelen;		/* length of name bytes */
	__u8	nameval[1];		/* name/value bytes */
} xfs_attr_leaf_name_local_t;

typedef struct xfs_attr_leaf_name_remote {
	__be32	valueblk;		/* block number of value bytes */
	__be32	valuelen;		/* number of bytes in value */
	__u8	namelen;		/* length of name bytes */
	__u8	name[1];		/* name bytes */
} xfs_attr_leaf_name_remote_t;

typedef struct xfs_attr_leafblock {
	xfs_attr_leaf_hdr_t	hdr;	/* constant-structure header block */
	xfs_attr_leaf_entry_t	entries[1];	/* sorted on key, not name */
	xfs_attr_leaf_name_local_t namelist;	/* grows from bottom of buf */
	xfs_attr_leaf_name_remote_t valuelist;	/* grows from bottom of buf */
} xfs_attr_leafblock_t;

/*
 * CRC enabled leaf structures. Called "version 3" structures to match the
 * version number of the directory and dablk structures for this feature, and
 * attr2 is already taken by the variable inode attribute fork size feature.
 */
struct xfs_attr3_leaf_hdr {
	struct xfs_da3_blkinfo	info;
	__be16			count;
	__be16			usedbytes;
	__be16			firstused;
	__u8			holes;
	__u8			pad1;
	struct xfs_attr_leaf_map freemap[XFS_ATTR_LEAF_MAPSIZE];
};

#define XFS_ATTR3_LEAF_CRC_OFF	(offsetof(struct xfs_attr3_leaf_hdr, info.crc))

struct xfs_attr3_leafblock {
	struct xfs_attr3_leaf_hdr	hdr;
	struct xfs_attr_leaf_entry	entries[1];

	/*
	 * The rest of the block contains the following structures after the
	 * leaf entries, growing from the bottom up. The variables are never
	 * referenced, the locations accessed purely from helper functions.
	 *
	 * struct xfs_attr_leaf_name_local
	 * struct xfs_attr_leaf_name_remote
	 */
};

/*
 * incore, neutral version of the attribute leaf header
 */
struct xfs_attr3_icleaf_hdr {
	__uint32_t	forw;
	__uint32_t	back;
	__uint16_t	magic;
	__uint16_t	count;
	__uint16_t	usedbytes;
	__uint16_t	firstused;
	__u8		holes;
	struct {
		__uint16_t	base;
		__uint16_t	size;
	} freemap[XFS_ATTR_LEAF_MAPSIZE];
};

/*
 * Flags used in the leaf_entry[i].flags field.
 * NOTE: the INCOMPLETE bit must not collide with the flags bits specified
 * on the system call, they are "or"ed together for various operations.
 */
#define	XFS_ATTR_LOCAL_BIT	0	/* attr is stored locally */
#define	XFS_ATTR_ROOT_BIT	1	/* limit access to trusted attrs */
#define	XFS_ATTR_SECURE_BIT	2	/* limit access to secure attrs */
#define	XFS_ATTR_INCOMPLETE_BIT	7	/* attr in middle of create/delete */
#define XFS_ATTR_LOCAL		(1 << XFS_ATTR_LOCAL_BIT)
#define XFS_ATTR_ROOT		(1 << XFS_ATTR_ROOT_BIT)
#define XFS_ATTR_SECURE		(1 << XFS_ATTR_SECURE_BIT)
#define XFS_ATTR_INCOMPLETE	(1 << XFS_ATTR_INCOMPLETE_BIT)

/*
 * Conversion macros for converting namespace bits from argument flags
 * to ondisk flags.
 */
#define XFS_ATTR_NSP_ARGS_MASK		(ATTR_ROOT | ATTR_SECURE)
#define XFS_ATTR_NSP_ONDISK_MASK	(XFS_ATTR_ROOT | XFS_ATTR_SECURE)
#define XFS_ATTR_NSP_ONDISK(flags)	((flags) & XFS_ATTR_NSP_ONDISK_MASK)
#define XFS_ATTR_NSP_ARGS(flags)	((flags) & XFS_ATTR_NSP_ARGS_MASK)
#define XFS_ATTR_NSP_ARGS_TO_ONDISK(x)	(((x) & ATTR_ROOT ? XFS_ATTR_ROOT : 0) |\
					 ((x) & ATTR_SECURE ? XFS_ATTR_SECURE : 0))
#define XFS_ATTR_NSP_ONDISK_TO_ARGS(x)	(((x) & XFS_ATTR_ROOT ? ATTR_ROOT : 0) |\
					 ((x) & XFS_ATTR_SECURE ? ATTR_SECURE : 0))

/*
 * Alignment for namelist and valuelist entries (since they are mixed
 * there can be only one alignment value)
 */
#define	XFS_ATTR_LEAF_NAME_ALIGN	((uint)sizeof(xfs_dablk_t))

static inline int
xfs_attr3_leaf_hdr_size(struct xfs_attr_leafblock *leafp)
{
	if (leafp->hdr.info.magic == cpu_to_be16(XFS_ATTR3_LEAF_MAGIC))
		return sizeof(struct xfs_attr3_leaf_hdr);
	return sizeof(struct xfs_attr_leaf_hdr);
}

static inline struct xfs_attr_leaf_entry *
xfs_attr3_leaf_entryp(xfs_attr_leafblock_t *leafp)
{
	if (leafp->hdr.info.magic == cpu_to_be16(XFS_ATTR3_LEAF_MAGIC))
		return &((struct xfs_attr3_leafblock *)leafp)->entries[0];
	return &leafp->entries[0];
}

/*
 * Cast typed pointers for "local" and "remote" name/value structs.
 */
static inline char *
xfs_attr3_leaf_name(xfs_attr_leafblock_t *leafp, int idx)
{
	struct xfs_attr_leaf_entry *entries = xfs_attr3_leaf_entryp(leafp);

	return &((char *)leafp)[be16_to_cpu(entries[idx].nameidx)];
}

static inline xfs_attr_leaf_name_remote_t *
xfs_attr3_leaf_name_remote(xfs_attr_leafblock_t *leafp, int idx)
{
	return (xfs_attr_leaf_name_remote_t *)xfs_attr3_leaf_name(leafp, idx);
}

static inline xfs_attr_leaf_name_local_t *
xfs_attr3_leaf_name_local(xfs_attr_leafblock_t *leafp, int idx)
{
	return (xfs_attr_leaf_name_local_t *)xfs_attr3_leaf_name(leafp, idx);
}

/*
 * Calculate total bytes used (including trailing pad for alignment) for
 * a "local" name/value structure, a "remote" name/value structure, and
 * a pointer which might be either.
 */
static inline int xfs_attr_leaf_entsize_remote(int nlen)
{
	return ((uint)sizeof(xfs_attr_leaf_name_remote_t) - 1 + (nlen) + \
		XFS_ATTR_LEAF_NAME_ALIGN - 1) & ~(XFS_ATTR_LEAF_NAME_ALIGN - 1);
}

static inline int xfs_attr_leaf_entsize_local(int nlen, int vlen)
{
	return ((uint)sizeof(xfs_attr_leaf_name_local_t) - 1 + (nlen) + (vlen) +
		XFS_ATTR_LEAF_NAME_ALIGN - 1) & ~(XFS_ATTR_LEAF_NAME_ALIGN - 1);
}

static inline int xfs_attr_leaf_entsize_local_max(int bsize)
{
	return (((bsize) >> 1) + ((bsize) >> 2));
}

/*
 * Used to keep a list of "remote value" extents when unlinking an inode.
 */
typedef struct xfs_attr_inactive_list {
	xfs_dablk_t	valueblk;	/* block number of value bytes */
	int		valuelen;	/* number of bytes in value */
} xfs_attr_inactive_list_t;


/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Internal routines when attribute fork size < XFS_LITINO(mp).
 */
void	xfs_attr_shortform_create(struct xfs_da_args *args);
void	xfs_attr_shortform_add(struct xfs_da_args *args, int forkoff);
int	xfs_attr_shortform_lookup(struct xfs_da_args *args);
int	xfs_attr_shortform_getvalue(struct xfs_da_args *args);
int	xfs_attr_shortform_to_leaf(struct xfs_da_args *args);
int	xfs_attr_shortform_remove(struct xfs_da_args *args);
int	xfs_attr_shortform_list(struct xfs_attr_list_context *context);
int	xfs_attr_shortform_allfit(struct xfs_buf *bp, struct xfs_inode *dp);
int	xfs_attr_shortform_bytesfit(xfs_inode_t *dp, int bytes);


/*
 * Internal routines when attribute fork size == XFS_LBSIZE(mp).
 */
int	xfs_attr3_leaf_to_node(struct xfs_da_args *args);
int	xfs_attr3_leaf_to_shortform(struct xfs_buf *bp,
				   struct xfs_da_args *args, int forkoff);
int	xfs_attr3_leaf_clearflag(struct xfs_da_args *args);
int	xfs_attr3_leaf_setflag(struct xfs_da_args *args);
int	xfs_attr3_leaf_flipflags(struct xfs_da_args *args);

/*
 * Routines used for growing the Btree.
 */
int	xfs_attr3_leaf_split(struct xfs_da_state *state,
				   struct xfs_da_state_blk *oldblk,
				   struct xfs_da_state_blk *newblk);
int	xfs_attr3_leaf_lookup_int(struct xfs_buf *leaf,
					struct xfs_da_args *args);
int	xfs_attr3_leaf_getvalue(struct xfs_buf *bp, struct xfs_da_args *args);
int	xfs_attr3_leaf_add(struct xfs_buf *leaf_buffer,
				 struct xfs_da_args *args);
int	xfs_attr3_leaf_remove(struct xfs_buf *leaf_buffer,
				    struct xfs_da_args *args);
int	xfs_attr3_leaf_list_int(struct xfs_buf *bp,
				      struct xfs_attr_list_context *context);

/*
 * Routines used for shrinking the Btree.
 */
int	xfs_attr3_leaf_toosmall(struct xfs_da_state *state, int *retval);
void	xfs_attr3_leaf_unbalance(struct xfs_da_state *state,
				       struct xfs_da_state_blk *drop_blk,
				       struct xfs_da_state_blk *save_blk);
int	xfs_attr3_root_inactive(struct xfs_trans **trans, struct xfs_inode *dp);

/*
 * Utility routines.
 */
xfs_dahash_t	xfs_attr_leaf_lasthash(struct xfs_buf *bp, int *count);
int	xfs_attr_leaf_order(struct xfs_buf *leaf1_bp,
				   struct xfs_buf *leaf2_bp);
int	xfs_attr_leaf_newentsize(int namelen, int valuelen, int blocksize,
					int *local);
int	xfs_attr3_leaf_read(struct xfs_trans *tp, struct xfs_inode *dp,
			xfs_dablk_t bno, xfs_daddr_t mappedbno,
			struct xfs_buf **bpp);
void	xfs_attr3_leaf_hdr_from_disk(struct xfs_attr3_icleaf_hdr *to,
				     struct xfs_attr_leafblock *from);

extern const struct xfs_buf_ops xfs_attr3_leaf_buf_ops;

#endif	/* __XFS_ATTR_LEAF_H__ */
