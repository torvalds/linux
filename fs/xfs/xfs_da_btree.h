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

struct xfs_buf;
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
 * Is is used to manage a doubly linked list of all blocks at the same
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

#define XFS_DA_MAXHASH	((xfs_dahash_t)-1) /* largest valid hash value */

#define	XFS_LBSIZE(mp)	(mp)->m_sb.sb_blocksize
#define	XFS_LBLOG(mp)	(mp)->m_sb.sb_blocklog

#define	XFS_DA_MAKE_BNOENTRY(mp,bno,entry)	\
	(((bno) << (mp)->m_dircook_elog) | (entry))
#define	XFS_DA_MAKE_COOKIE(mp,bno,entry,hash)	\
	(((xfs_off_t)XFS_DA_MAKE_BNOENTRY(mp, bno, entry) << 32) | (hash))
#define	XFS_DA_COOKIE_HASH(mp,cookie)		((xfs_dahash_t)cookie)
#define	XFS_DA_COOKIE_BNO(mp,cookie)		\
	((((xfs_off_t)(cookie) >> 31) == -1LL ? \
		(xfs_dablk_t)0 : \
		(xfs_dablk_t)((xfs_off_t)(cookie) >> \
				((mp)->m_dircook_elog + 32))))
#define	XFS_DA_COOKIE_ENTRY(mp,cookie)		\
	((((xfs_off_t)(cookie) >> 31) == -1LL ?	\
		(xfs_dablk_t)0 : \
		(xfs_dablk_t)(((xfs_off_t)(cookie) >> 32) & \
				((1 << (mp)->m_dircook_elog) - 1))))


/*========================================================================
 * Btree searching and modification structure definitions.
 *========================================================================*/

/*
 * Structure to ease passing around component names.
 */
typedef struct xfs_da_args {
	const uchar_t	*name;		/* string (maybe not NULL terminated) */
	int		namelen;	/* length of string (maybe no NULL) */
	uchar_t		*value;		/* set of bytes (maybe contain NULLs) */
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
	unsigned char	justcheck;	/* T/F: check for ok with no space */
	unsigned char	rename;		/* T/F: this is an atomic rename op */
	unsigned char	addname;	/* T/F: this is an add operation */
	unsigned char	oknoent;	/* T/F: ok to return ENOENT, else die */
} xfs_da_args_t;

/*
 * Structure to describe buffer(s) for a block.
 * This is needed in the directory version 2 format case, when
 * multiple non-contiguous fsblocks might be needed to cover one
 * logical directory block.
 * If the buffer count is 1 then the data pointer points to the
 * same place as the b_addr field for the buffer, else to kmem_alloced memory.
 */
typedef struct xfs_dabuf {
	int		nbuf;		/* number of buffer pointers present */
	short		dirty;		/* data needs to be copied back */
	short		bbcount;	/* how large is data in bbs */
	void		*data;		/* pointer for buffers' data */
#ifdef XFS_DABUF_DEBUG
	inst_t		*ra;		/* return address of caller to make */
	struct xfs_dabuf *next;		/* next in global chain */
	struct xfs_dabuf *prev;		/* previous in global chain */
	struct xfs_buftarg *target;	/* device for buffer */
	xfs_daddr_t	blkno;		/* daddr first in bps[0] */
#endif
	struct xfs_buf	*bps[1];	/* actually nbuf of these */
} xfs_dabuf_t;
#define	XFS_DA_BUF_SIZE(n)	\
	(sizeof(xfs_dabuf_t) + sizeof(struct xfs_buf *) * ((n) - 1))

#ifdef XFS_DABUF_DEBUG
extern xfs_dabuf_t	*xfs_dabuf_global_list;
#endif

/*
 * Storage for holding state during Btree searches and split/join ops.
 *
 * Only need space for 5 intermediate nodes.  With a minimum of 62-way
 * fanout to the Btree, we can support over 900 million directory blocks,
 * which is slightly more than enough.
 */
typedef struct xfs_da_state_blk {
	xfs_dabuf_t	*bp;		/* buffer containing block */
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
	xfs_da_state_blk_t	extrablk;	/* for double-splits on leafs */
						/* for dirv2 extrablk is data */
} xfs_da_state_t;

/*
 * Utility macros to aid in logging changed structure fields.
 */
#define XFS_DA_LOGOFF(BASE, ADDR)	((char *)(ADDR) - (char *)(BASE))
#define XFS_DA_LOGRANGE(BASE, ADDR, SIZE)	\
		(uint)(XFS_DA_LOGOFF(BASE, ADDR)), \
		(uint)(XFS_DA_LOGOFF(BASE, ADDR)+(SIZE)-1)


#ifdef __KERNEL__
/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
int	xfs_da_node_create(xfs_da_args_t *args, xfs_dablk_t blkno, int level,
					 xfs_dabuf_t **bpp, int whichfork);
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

/*
 * Utility routines.
 */
int	xfs_da_grow_inode(xfs_da_args_t *args, xfs_dablk_t *new_blkno);
int	xfs_da_get_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			      xfs_dablk_t bno, xfs_daddr_t mappedbno,
			      xfs_dabuf_t **bp, int whichfork);
int	xfs_da_read_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			       xfs_dablk_t bno, xfs_daddr_t mappedbno,
			       xfs_dabuf_t **bpp, int whichfork);
xfs_daddr_t	xfs_da_reada_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			xfs_dablk_t bno, int whichfork);
int	xfs_da_shrink_inode(xfs_da_args_t *args, xfs_dablk_t dead_blkno,
					  xfs_dabuf_t *dead_buf);

uint xfs_da_hashname(const uchar_t *name_string, int name_length);
xfs_da_state_t *xfs_da_state_alloc(void);
void xfs_da_state_free(xfs_da_state_t *state);

void xfs_da_buf_done(xfs_dabuf_t *dabuf);
void xfs_da_log_buf(struct xfs_trans *tp, xfs_dabuf_t *dabuf, uint first,
			   uint last);
void xfs_da_brelse(struct xfs_trans *tp, xfs_dabuf_t *dabuf);
void xfs_da_binval(struct xfs_trans *tp, xfs_dabuf_t *dabuf);
xfs_daddr_t xfs_da_blkno(xfs_dabuf_t *dabuf);

extern struct kmem_zone *xfs_da_state_zone;
extern struct kmem_zone *xfs_dabuf_zone;
#endif	/* __KERNEL__ */

#endif	/* __XFS_DA_BTREE_H__ */
