/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_DA_BTREE_H__
#define	__XFS_DA_BTREE_H__

struct xfs_inode;
struct xfs_trans;

/*
 * Directory/attribute geometry information. There will be one of these for each
 * data fork type, and it will be passed around via the xfs_da_args. Global
 * structures will be attached to the xfs_mount.
 */
struct xfs_da_geometry {
	unsigned int	blksize;	/* da block size in bytes */
	unsigned int	fsbcount;	/* da block size in filesystem blocks */
	uint8_t		fsblog;		/* log2 of _filesystem_ block size */
	uint8_t		blklog;		/* log2 of da block size */
	unsigned int	node_hdr_size;	/* danode header size in bytes */
	unsigned int	node_ents;	/* # of entries in a danode */
	unsigned int	magicpct;	/* 37% of block size in bytes */
	xfs_dablk_t	datablk;	/* blockno of dir data v2 */
	unsigned int	leaf_hdr_size;	/* dir2 leaf header size */
	unsigned int	leaf_max_ents;	/* # of entries in dir2 leaf */
	xfs_dablk_t	leafblk;	/* blockno of leaf data v2 */
	unsigned int	free_hdr_size;	/* dir2 free header size */
	unsigned int	free_max_bests;	/* # of bests entries in dir2 free */
	xfs_dablk_t	freeblk;	/* blockno of free data v2 */

	xfs_dir2_data_aoff_t data_first_offset;
	size_t		data_entry_offset;
};

/*========================================================================
 * Btree searching and modification structure definitions.
 *========================================================================*/

/*
 * Search comparison results
 */
enum xfs_dacmp {
	XFS_CMP_DIFFERENT,	/* names are completely different */
	XFS_CMP_EXACT,		/* names are exactly the same */
	XFS_CMP_CASE		/* names are same but differ in case */
};

/*
 * Structure to ease passing around component names.
 */
typedef struct xfs_da_args {
	struct xfs_da_geometry *geo;	/* da block geometry */
	const uint8_t		*name;		/* string (maybe not NULL terminated) */
	int		namelen;	/* length of string (maybe no NULL) */
	uint8_t		filetype;	/* filetype of inode for directories */
	void		*value;		/* set of bytes (maybe contain NULLs) */
	int		valuelen;	/* length of value */
	unsigned int	attr_filter;	/* XFS_ATTR_{ROOT,SECURE,INCOMPLETE} */
	unsigned int	attr_flags;	/* XATTR_{CREATE,REPLACE} */
	xfs_dahash_t	hashval;	/* hash value of name */
	xfs_ino_t	inumber;	/* input/output inode number */
	struct xfs_inode *dp;		/* directory inode to manipulate */
	struct xfs_trans *trans;	/* current trans (changes over time) */
	xfs_extlen_t	total;		/* total blocks needed, for 1st bmap */
	int		whichfork;	/* data or attribute fork */
	xfs_dablk_t	blkno;		/* blkno of attr leaf of interest */
	int		index;		/* index of attr of interest in blk */
	xfs_dablk_t	rmtblkno;	/* remote attr value starting blkno */
	int		rmtblkcnt;	/* remote attr value block count */
	int		rmtvaluelen;	/* remote attr value length in bytes */
	xfs_dablk_t	blkno2;		/* blkno of 2nd attr leaf of interest */
	int		index2;		/* index of 2nd attr in blk */
	xfs_dablk_t	rmtblkno2;	/* remote attr value starting blkno */
	int		rmtblkcnt2;	/* remote attr value block count */
	int		rmtvaluelen2;	/* remote attr value length in bytes */
	int		op_flags;	/* operation flags */
	enum xfs_dacmp	cmpresult;	/* name compare result for lookups */
} xfs_da_args_t;

/*
 * Operation flags:
 */
#define XFS_DA_OP_JUSTCHECK	0x0001	/* check for ok with no space */
#define XFS_DA_OP_RENAME	0x0002	/* this is an atomic rename op */
#define XFS_DA_OP_ADDNAME	0x0004	/* this is an add operation */
#define XFS_DA_OP_OKNOENT	0x0008	/* lookup/add op, ENOENT ok, else die */
#define XFS_DA_OP_CILOOKUP	0x0010	/* lookup to return CI name if found */
#define XFS_DA_OP_NOTIME	0x0020	/* don't update inode timestamps */

#define XFS_DA_OP_FLAGS \
	{ XFS_DA_OP_JUSTCHECK,	"JUSTCHECK" }, \
	{ XFS_DA_OP_RENAME,	"RENAME" }, \
	{ XFS_DA_OP_ADDNAME,	"ADDNAME" }, \
	{ XFS_DA_OP_OKNOENT,	"OKNOENT" }, \
	{ XFS_DA_OP_CILOOKUP,	"CILOOKUP" }, \
	{ XFS_DA_OP_NOTIME,	"NOTIME" }

/*
 * Storage for holding state during Btree searches and split/join ops.
 *
 * Only need space for 5 intermediate nodes.  With a minimum of 62-way
 * fanout to the Btree, we can support over 900 million directory blocks,
 * which is slightly more than enough.
 */
typedef struct xfs_da_state_blk {
	struct xfs_buf	*bp;		/* buffer containing block */
	xfs_dablk_t	blkno;		/* filesystem blkno of buffer */
	xfs_daddr_t	disk_blkno;	/* on-disk blkno (in BBs) of buffer */
	int		index;		/* relevant index into block */
	xfs_dahash_t	hashval;	/* last hash value in block */
	int		magic;		/* blk's magic number, ie: blk type */
} xfs_da_state_blk_t;

typedef struct xfs_da_state_path {
	int			active;		/* number of active levels */
	xfs_da_state_blk_t	blk[XFS_DA_NODE_MAXDEPTH];
} xfs_da_state_path_t;

typedef struct xfs_da_state {
	xfs_da_args_t		*args;		/* filename arguments */
	struct xfs_mount	*mp;		/* filesystem mount point */
	xfs_da_state_path_t	path;		/* search/split paths */
	xfs_da_state_path_t	altpath;	/* alternate path for join */
	unsigned char		inleaf;		/* insert into 1->lf, 0->splf */
	unsigned char		extravalid;	/* T/F: extrablk is in use */
	unsigned char		extraafter;	/* T/F: extrablk is after new */
	xfs_da_state_blk_t	extrablk;	/* for double-splits on leaves */
						/* for dirv2 extrablk is data */
} xfs_da_state_t;

/*
 * In-core version of the node header to abstract the differences in the v2 and
 * v3 disk format of the headers. Callers need to convert to/from disk format as
 * appropriate.
 */
struct xfs_da3_icnode_hdr {
	uint32_t		forw;
	uint32_t		back;
	uint16_t		magic;
	uint16_t		count;
	uint16_t		level;

	/*
	 * Pointer to the on-disk format entries, which are behind the
	 * variable size (v4 vs v5) header in the on-disk block.
	 */
	struct xfs_da_node_entry *btree;
};

/*
 * Utility macros to aid in logging changed structure fields.
 */
#define XFS_DA_LOGOFF(BASE, ADDR)	((char *)(ADDR) - (char *)(BASE))
#define XFS_DA_LOGRANGE(BASE, ADDR, SIZE)	\
		(uint)(XFS_DA_LOGOFF(BASE, ADDR)), \
		(uint)(XFS_DA_LOGOFF(BASE, ADDR)+(SIZE)-1)

/*========================================================================
 * Function prototypes.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
int	xfs_da3_node_create(struct xfs_da_args *args, xfs_dablk_t blkno,
			    int level, struct xfs_buf **bpp, int whichfork);
int	xfs_da3_split(xfs_da_state_t *state);

/*
 * Routines used for shrinking the Btree.
 */
int	xfs_da3_join(xfs_da_state_t *state);
void	xfs_da3_fixhashpath(struct xfs_da_state *state,
			    struct xfs_da_state_path *path_to_to_fix);

/*
 * Routines used for finding things in the Btree.
 */
int	xfs_da3_node_lookup_int(xfs_da_state_t *state, int *result);
int	xfs_da3_path_shift(xfs_da_state_t *state, xfs_da_state_path_t *path,
					 int forward, int release, int *result);
/*
 * Utility routines.
 */
int	xfs_da3_blk_link(xfs_da_state_t *state, xfs_da_state_blk_t *old_blk,
				       xfs_da_state_blk_t *new_blk);
int	xfs_da3_node_read(struct xfs_trans *tp, struct xfs_inode *dp,
			xfs_dablk_t bno, struct xfs_buf **bpp, int whichfork);
int	xfs_da3_node_read_mapped(struct xfs_trans *tp, struct xfs_inode *dp,
			xfs_daddr_t mappedbno, struct xfs_buf **bpp,
			int whichfork);

/*
 * Utility routines.
 */

#define XFS_DABUF_MAP_HOLE_OK	(1 << 0)

int	xfs_da_grow_inode(xfs_da_args_t *args, xfs_dablk_t *new_blkno);
int	xfs_da_grow_inode_int(struct xfs_da_args *args, xfs_fileoff_t *bno,
			      int count);
int	xfs_da_get_buf(struct xfs_trans *trans, struct xfs_inode *dp,
		xfs_dablk_t bno, struct xfs_buf **bp, int whichfork);
int	xfs_da_read_buf(struct xfs_trans *trans, struct xfs_inode *dp,
		xfs_dablk_t bno, unsigned int flags, struct xfs_buf **bpp,
		int whichfork, const struct xfs_buf_ops *ops);
int	xfs_da_reada_buf(struct xfs_inode *dp, xfs_dablk_t bno,
		unsigned int flags, int whichfork,
		const struct xfs_buf_ops *ops);
int	xfs_da_shrink_inode(xfs_da_args_t *args, xfs_dablk_t dead_blkno,
					  struct xfs_buf *dead_buf);

uint xfs_da_hashname(const uint8_t *name_string, int name_length);
enum xfs_dacmp xfs_da_compname(struct xfs_da_args *args,
				const unsigned char *name, int len);


struct xfs_da_state *xfs_da_state_alloc(struct xfs_da_args *args);
void xfs_da_state_free(xfs_da_state_t *state);

void	xfs_da3_node_hdr_from_disk(struct xfs_mount *mp,
		struct xfs_da3_icnode_hdr *to, struct xfs_da_intnode *from);
void	xfs_da3_node_hdr_to_disk(struct xfs_mount *mp,
		struct xfs_da_intnode *to, struct xfs_da3_icnode_hdr *from);

extern struct kmem_cache	*xfs_da_state_cache;

#endif	/* __XFS_DA_BTREE_H__ */
