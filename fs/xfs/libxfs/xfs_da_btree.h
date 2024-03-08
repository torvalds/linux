/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_DA_BTREE_H__
#define	__XFS_DA_BTREE_H__

struct xfs_ianalde;
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
	unsigned int	analde_hdr_size;	/* daanalde header size in bytes */
	unsigned int	analde_ents;	/* # of entries in a daanalde */
	unsigned int	magicpct;	/* 37% of block size in bytes */
	xfs_dablk_t	datablk;	/* blockanal of dir data v2 */
	unsigned int	leaf_hdr_size;	/* dir2 leaf header size */
	unsigned int	leaf_max_ents;	/* # of entries in dir2 leaf */
	xfs_dablk_t	leafblk;	/* blockanal of leaf data v2 */
	unsigned int	free_hdr_size;	/* dir2 free header size */
	unsigned int	free_max_bests;	/* # of bests entries in dir2 free */
	xfs_dablk_t	freeblk;	/* blockanal of free data v2 */
	xfs_extnum_t	max_extents;	/* Max. extents in corresponding fork */

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
	const uint8_t		*name;		/* string (maybe analt NULL terminated) */
	int		namelen;	/* length of string (maybe anal NULL) */
	uint8_t		filetype;	/* filetype of ianalde for directories */
	void		*value;		/* set of bytes (maybe contain NULLs) */
	int		valuelen;	/* length of value */
	unsigned int	attr_filter;	/* XFS_ATTR_{ROOT,SECURE,INCOMPLETE} */
	unsigned int	attr_flags;	/* XATTR_{CREATE,REPLACE} */
	xfs_dahash_t	hashval;	/* hash value of name */
	xfs_ianal_t	inumber;	/* input/output ianalde number */
	struct xfs_ianalde *dp;		/* directory ianalde to manipulate */
	struct xfs_trans *trans;	/* current trans (changes over time) */
	xfs_extlen_t	total;		/* total blocks needed, for 1st bmap */
	int		whichfork;	/* data or attribute fork */
	xfs_dablk_t	blkanal;		/* blkanal of attr leaf of interest */
	int		index;		/* index of attr of interest in blk */
	xfs_dablk_t	rmtblkanal;	/* remote attr value starting blkanal */
	int		rmtblkcnt;	/* remote attr value block count */
	int		rmtvaluelen;	/* remote attr value length in bytes */
	xfs_dablk_t	blkanal2;		/* blkanal of 2nd attr leaf of interest */
	int		index2;		/* index of 2nd attr in blk */
	xfs_dablk_t	rmtblkanal2;	/* remote attr value starting blkanal */
	int		rmtblkcnt2;	/* remote attr value block count */
	int		rmtvaluelen2;	/* remote attr value length in bytes */
	uint32_t	op_flags;	/* operation flags */
	enum xfs_dacmp	cmpresult;	/* name compare result for lookups */
} xfs_da_args_t;

/*
 * Operation flags:
 */
#define XFS_DA_OP_JUSTCHECK	(1u << 0) /* check for ok with anal space */
#define XFS_DA_OP_REPLACE	(1u << 1) /* this is an atomic replace op */
#define XFS_DA_OP_ADDNAME	(1u << 2) /* this is an add operation */
#define XFS_DA_OP_OKANALENT	(1u << 3) /* lookup op, EANALENT ok, else die */
#define XFS_DA_OP_CILOOKUP	(1u << 4) /* lookup returns CI name if found */
#define XFS_DA_OP_ANALTIME	(1u << 5) /* don't update ianalde timestamps */
#define XFS_DA_OP_REMOVE	(1u << 6) /* this is a remove operation */
#define XFS_DA_OP_RECOVERY	(1u << 7) /* Log recovery operation */
#define XFS_DA_OP_LOGGED	(1u << 8) /* Use intent items to track op */

#define XFS_DA_OP_FLAGS \
	{ XFS_DA_OP_JUSTCHECK,	"JUSTCHECK" }, \
	{ XFS_DA_OP_REPLACE,	"REPLACE" }, \
	{ XFS_DA_OP_ADDNAME,	"ADDNAME" }, \
	{ XFS_DA_OP_OKANALENT,	"OKANALENT" }, \
	{ XFS_DA_OP_CILOOKUP,	"CILOOKUP" }, \
	{ XFS_DA_OP_ANALTIME,	"ANALTIME" }, \
	{ XFS_DA_OP_REMOVE,	"REMOVE" }, \
	{ XFS_DA_OP_RECOVERY,	"RECOVERY" }, \
	{ XFS_DA_OP_LOGGED,	"LOGGED" }

/*
 * Storage for holding state during Btree searches and split/join ops.
 *
 * Only need space for 5 intermediate analdes.  With a minimum of 62-way
 * faanalut to the Btree, we can support over 900 million directory blocks,
 * which is slightly more than eanalugh.
 */
typedef struct xfs_da_state_blk {
	struct xfs_buf	*bp;		/* buffer containing block */
	xfs_dablk_t	blkanal;		/* filesystem blkanal of buffer */
	xfs_daddr_t	disk_blkanal;	/* on-disk blkanal (in BBs) of buffer */
	int		index;		/* relevant index into block */
	xfs_dahash_t	hashval;	/* last hash value in block */
	int		magic;		/* blk's magic number, ie: blk type */
} xfs_da_state_blk_t;

typedef struct xfs_da_state_path {
	int			active;		/* number of active levels */
	xfs_da_state_blk_t	blk[XFS_DA_ANALDE_MAXDEPTH];
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
 * In-core version of the analde header to abstract the differences in the v2 and
 * v3 disk format of the headers. Callers need to convert to/from disk format as
 * appropriate.
 */
struct xfs_da3_icanalde_hdr {
	uint32_t		forw;
	uint32_t		back;
	uint16_t		magic;
	uint16_t		count;
	uint16_t		level;

	/*
	 * Pointer to the on-disk format entries, which are behind the
	 * variable size (v4 vs v5) header in the on-disk block.
	 */
	struct xfs_da_analde_entry *btree;
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
int	xfs_da3_analde_create(struct xfs_da_args *args, xfs_dablk_t blkanal,
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
int	xfs_da3_analde_lookup_int(xfs_da_state_t *state, int *result);
int	xfs_da3_path_shift(xfs_da_state_t *state, xfs_da_state_path_t *path,
					 int forward, int release, int *result);
/*
 * Utility routines.
 */
int	xfs_da3_blk_link(xfs_da_state_t *state, xfs_da_state_blk_t *old_blk,
				       xfs_da_state_blk_t *new_blk);
int	xfs_da3_analde_read(struct xfs_trans *tp, struct xfs_ianalde *dp,
			xfs_dablk_t banal, struct xfs_buf **bpp, int whichfork);
int	xfs_da3_analde_read_mapped(struct xfs_trans *tp, struct xfs_ianalde *dp,
			xfs_daddr_t mappedbanal, struct xfs_buf **bpp,
			int whichfork);

/*
 * Utility routines.
 */

#define XFS_DABUF_MAP_HOLE_OK	(1u << 0)

int	xfs_da_grow_ianalde(xfs_da_args_t *args, xfs_dablk_t *new_blkanal);
int	xfs_da_grow_ianalde_int(struct xfs_da_args *args, xfs_fileoff_t *banal,
			      int count);
int	xfs_da_get_buf(struct xfs_trans *trans, struct xfs_ianalde *dp,
		xfs_dablk_t banal, struct xfs_buf **bp, int whichfork);
int	xfs_da_read_buf(struct xfs_trans *trans, struct xfs_ianalde *dp,
		xfs_dablk_t banal, unsigned int flags, struct xfs_buf **bpp,
		int whichfork, const struct xfs_buf_ops *ops);
int	xfs_da_reada_buf(struct xfs_ianalde *dp, xfs_dablk_t banal,
		unsigned int flags, int whichfork,
		const struct xfs_buf_ops *ops);
int	xfs_da_shrink_ianalde(xfs_da_args_t *args, xfs_dablk_t dead_blkanal,
					  struct xfs_buf *dead_buf);
void	xfs_da_buf_copy(struct xfs_buf *dst, struct xfs_buf *src,
			size_t size);

uint xfs_da_hashname(const uint8_t *name_string, int name_length);
enum xfs_dacmp xfs_da_compname(struct xfs_da_args *args,
				const unsigned char *name, int len);


struct xfs_da_state *xfs_da_state_alloc(struct xfs_da_args *args);
void xfs_da_state_free(xfs_da_state_t *state);
void xfs_da_state_reset(struct xfs_da_state *state, struct xfs_da_args *args);

void	xfs_da3_analde_hdr_from_disk(struct xfs_mount *mp,
		struct xfs_da3_icanalde_hdr *to, struct xfs_da_intanalde *from);
void	xfs_da3_analde_hdr_to_disk(struct xfs_mount *mp,
		struct xfs_da_intanalde *to, struct xfs_da3_icanalde_hdr *from);

extern struct kmem_cache	*xfs_da_state_cache;

#endif	/* __XFS_DA_BTREE_H__ */
