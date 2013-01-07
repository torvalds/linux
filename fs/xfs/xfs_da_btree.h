/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_DA_BTREE_H__
#define	__XFS_DA_BTREE_H__

struct xfs_bmap_free;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;
struct zone;

/*========================================================================
 * Directory Structure when greater than XFS_LBSIZE(mp) bytes.
 *========================================================================*/

/*
 * This structure is common to both leaf nodes and non-leaf nodes in the Btree.
 *
 * It is used to manage a doubly linked list of all blocks at the same
 * level in the Btree, and to identify which type of block this is.
 */
#define XFS_DA_NODE_MAGIC	0xfebe	/* magic number: non-leaf blocks */
#define XFS_ATTR_LEAF_MAGIC	0xfbee	/* magic number: attribute leaf blks */
#define	XFS_DIR2_LEAF1_MAGIC	0xd2f1	/* magic number: v2 dirlf single blks */
#define	XFS_DIR2_LEAFN_MAGIC	0xd2ff	/* magic number: v2 dirlf multi blks */

typedef struct xfs_da_blkinfo {
	__be32		forw;			/* previous block in list */
	__be32		back;			/* following block in list */
	__be16		magic;			/* validity check on block */
	__be16		pad;			/* unused */
} xfs_da_blkinfo_t;

/*
 * This is the structure of the root and intermediate nodes in the Btree.
 * The leaf nodes are defined above.
 *
 * Entries are not packed.
 *
 * Since we have duplicate keys, use a binary search but always follow
 * all match in the block, not just the first match found.
 */
#define	XFS_DA_NODE_MAXDEPTH	5	/* max depth of Btree */

typedef struct xfs_da_intnode {
	struct xfs_da_node_hdr {	/* constant-structure header block */
		xfs_da_blkinfo_t info;	/* block type, links, etc. */
		__be16	count;		/* count of active entries */
		__be16	level;		/* level above leaves (leaf == 0) */
	} hdr;
	struct xfs_da_node_entry {
		__be32	hashval;	/* hash value for this descendant */
		__be32	before;		/* Btree block before this key */
	} btree[1];			/* variable sized array of keys */
} xfs_da_intnode_t;
typedef struct xfs_da_node_hdr xfs_da_node_hdr_t;
typedef struct xfs_da_node_entry xfs_da_node_entry_t;

#define	XFS_LBSIZE(mp)	(mp)->m_sb.sb_blocksize

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
	const __uint8_t	*name;		/* string (maybe not NULL terminated) */
	int		namelen;	/* length of string (maybe no NULL) */
	__uint8_t	*value;		/* set of bytes (maybe contain NULLs) */
	int		valuelen;	/* length of value */
	int		flags;		/* argument flags (eg: ATTR_NOCREATE) */
	xfs_dahash_t	hashval;	/* hash value of name */
	xfs_ino_t	inumber;	/* input/output inode number */
	struct xfs_inode *dp;		/* directory inode to manipulate */
	xfs_fsblock_t	*firstblock;	/* ptr to firstblock for bmap calls */
	struct xfs_bmap_free *flist;	/* ptr to freelist for bmap_finish */
	struct xfs_trans *trans;	/* current trans (changes over time) */
	xfs_extlen_t	total;		/* total blocks needed, for 1st bmap */
	int		whichfork;	/* data or attribute fork */
	xfs_dablk_t	blkno;		/* blkno of attr leaf of interest */
	int		index;		/* index of attr of interest in blk */
	xfs_dablk_t	rmtblkno;	/* remote attr value starting blkno */
	int		rmtblkcnt;	/* remote attr value block count */
	xfs_dablk_t	blkno2;		/* blkno of 2nd attr leaf of interest */
	int		index2;		/* index of 2nd attr in blk */
	xfs_dablk_t	rmtblkno2;	/* remote attr value starting blkno */
	int		rmtblkcnt2;	/* remote attr value block count */
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

#define XFS_DA_OP_FLAGS \
	{ XFS_DA_OP_JUSTCHECK,	"JUSTCHECK" }, \
	{ XFS_DA_OP_RENAME,	"RENAME" }, \
	{ XFS_DA_OP_ADDNAME,	"ADDNAME" }, \
	{ XFS_DA_OP_OKNOENT,	"OKNOENT" }, \
	{ XFS_DA_OP_CILOOKUP,	"CILOOKUP" }

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
	unsigned int		blocksize;	/* logical block size */
	unsigned int		node_ents;	/* how many entries in danode */
	xfs_da_state_path_t	path;		/* search/split paths */
	xfs_da_state_path_t	altpath;	/* alternate path for join */
	unsigned char		inleaf;		/* insert into 1->lf, 0->splf */
	unsigned char		extravalid;	/* T/F: extrablk is in use */
	unsigned char		extraafter;	/* T/F: extrablk is after new */
	xfs_da_state_blk_t	extrablk;	/* for double-splits on leaves */
						/* for dirv2 extrablk is data */
} xfs_da_state_t;

/*
 * Utility macros to aid in logging changed structure fields.
 */
#define XFS_DA_LOGOFF(BASE, ADDR)	((char *)(ADDR) - (char *)(BASE))
#define XFS_DA_LOGRANGE(BASE, ADDR, SIZE)	\
		(uint)(XFS_DA_LOGOFF(BASE, ADDR)), \
		(uint)(XFS_DA_LOGOFF(BASE, ADDR)+(SIZE)-1)

/*
 * Name ops for directory and/or attr name operations
 */
struct xfs_nameops {
	xfs_dahash_t	(*hashname)(struct xfs_name *);
	enum xfs_dacmp	(*compname)(struct xfs_da_args *,
					const unsigned char *, int);
};


/*========================================================================
 * Function prototypes.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
int	xfs_da_node_create(xfs_da_args_t *args, xfs_dablk_t blkno, int level,
					 struct xfs_buf **bpp, int whichfork);
int	xfs_da_split(xfs_da_state_t *state);

/*
 * Routines used for shrinking the Btree.
 */
int	xfs_da_join(xfs_da_state_t *state);
void	xfs_da_fixhashpath(xfs_da_state_t *state,
					  xfs_da_state_path_t *path_to_to_fix);

/*
 * Routines used for finding things in the Btree.
 */
int	xfs_da_node_lookup_int(xfs_da_state_t *state, int *result);
int	xfs_da_path_shift(xfs_da_state_t *state, xfs_da_state_path_t *path,
					 int forward, int release, int *result);
/*
 * Utility routines.
 */
int	xfs_da_blk_link(xfs_da_state_t *state, xfs_da_state_blk_t *old_blk,
				       xfs_da_state_blk_t *new_blk);
int	xfs_da_node_read(struct xfs_trans *tp, struct xfs_inode *dp,
			 xfs_dablk_t bno, xfs_daddr_t mappedbno,
			 struct xfs_buf **bpp, int which_fork);

/*
 * Utility routines.
 */
int	xfs_da_grow_inode(xfs_da_args_t *args, xfs_dablk_t *new_blkno);
int	xfs_da_grow_inode_int(struct xfs_da_args *args, xfs_fileoff_t *bno,
			      int count);
int	xfs_da_get_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			      xfs_dablk_t bno, xfs_daddr_t mappedbno,
			      struct xfs_buf **bp, int whichfork);
int	xfs_da_read_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			       xfs_dablk_t bno, xfs_daddr_t mappedbno,
			       struct xfs_buf **bpp, int whichfork,
			       const struct xfs_buf_ops *ops);
xfs_daddr_t	xfs_da_reada_buf(struct xfs_trans *trans, struct xfs_inode *dp,
				xfs_dablk_t bno, xfs_daddr_t mapped_bno,
				int whichfork, const struct xfs_buf_ops *ops);
int	xfs_da_shrink_inode(xfs_da_args_t *args, xfs_dablk_t dead_blkno,
					  struct xfs_buf *dead_buf);

uint xfs_da_hashname(const __uint8_t *name_string, int name_length);
enum xfs_dacmp xfs_da_compname(struct xfs_da_args *args,
				const unsigned char *name, int len);


xfs_da_state_t *xfs_da_state_alloc(void);
void xfs_da_state_free(xfs_da_state_t *state);

extern struct kmem_zone *xfs_da_state_zone;
extern const struct xfs_nameops xfs_default_nameops;

#endif	/* __XFS_DA_BTREE_H__ */
