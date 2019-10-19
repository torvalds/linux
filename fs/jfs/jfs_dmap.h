/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2002
 */
#ifndef	_H_JFS_DMAP
#define _H_JFS_DMAP

#include "jfs_txnmgr.h"

#define BMAPVERSION	1	/* version number */
#define	TREESIZE	(256+64+16+4+1)	/* size of a dmap tree */
#define	LEAFIND		(64+16+4+1)	/* index of 1st leaf of a dmap tree */
#define LPERDMAP	256	/* num leaves per dmap tree */
#define L2LPERDMAP	8	/* l2 number of leaves per dmap tree */
#define	DBWORD		32	/* # of blks covered by a map word */
#define	L2DBWORD	5	/* l2 # of blks covered by a mword */
#define BUDMIN		L2DBWORD	/* max free string in a map word */
#define BPERDMAP	(LPERDMAP * DBWORD)	/* num of blks per dmap */
#define L2BPERDMAP	13	/* l2 num of blks per dmap */
#define CTLTREESIZE	(1024+256+64+16+4+1)	/* size of a dmapctl tree */
#define CTLLEAFIND	(256+64+16+4+1)	/* idx of 1st leaf of a dmapctl tree */
#define LPERCTL		1024	/* num of leaves per dmapctl tree */
#define L2LPERCTL	10	/* l2 num of leaves per dmapctl tree */
#define	ROOT		0	/* index of the root of a tree */
#define	NOFREE		((s8) -1)	/* no blocks free */
#define	MAXAG		128	/* max number of allocation groups */
#define L2MAXAG		7	/* l2 max num of AG */
#define L2MINAGSZ	25	/* l2 of minimum AG size in bytes */
#define	BMAPBLKNO	0	/* lblkno of bmap within the map */

/*
 * maximum l2 number of disk blocks at the various dmapctl levels.
 */
#define	L2MAXL0SIZE	(L2BPERDMAP + 1 * L2LPERCTL)
#define	L2MAXL1SIZE	(L2BPERDMAP + 2 * L2LPERCTL)
#define	L2MAXL2SIZE	(L2BPERDMAP + 3 * L2LPERCTL)

/*
 * maximum number of disk blocks at the various dmapctl levels.
 */
#define	MAXL0SIZE	((s64)1 << L2MAXL0SIZE)
#define	MAXL1SIZE	((s64)1 << L2MAXL1SIZE)
#define	MAXL2SIZE	((s64)1 << L2MAXL2SIZE)

#define	MAXMAPSIZE	MAXL2SIZE	/* maximum aggregate map size */

/*
 * determine the maximum free string for four (lower level) nodes
 * of the tree.
 */
static inline signed char TREEMAX(signed char *cp)
{
	signed char tmp1, tmp2;

	tmp1 = max(*(cp+2), *(cp+3));
	tmp2 = max(*(cp), *(cp+1));

	return max(tmp1, tmp2);
}

/*
 * convert disk block number to the logical block number of the dmap
 * describing the disk block.  s is the log2(number of logical blocks per page)
 *
 * The calculation figures out how many logical pages are in front of the dmap.
 *	- the number of dmaps preceding it
 *	- the number of L0 pages preceding its L0 page
 *	- the number of L1 pages preceding its L1 page
 *	- 3 is added to account for the L2, L1, and L0 page for this dmap
 *	- 1 is added to account for the control page of the map.
 */
#define BLKTODMAP(b,s)    \
	((((b) >> 13) + ((b) >> 23) + ((b) >> 33) + 3 + 1) << (s))

/*
 * convert disk block number to the logical block number of the LEVEL 0
 * dmapctl describing the disk block.  s is the log2(number of logical blocks
 * per page)
 *
 * The calculation figures out how many logical pages are in front of the L0.
 *	- the number of dmap pages preceding it
 *	- the number of L0 pages preceding it
 *	- the number of L1 pages preceding its L1 page
 *	- 2 is added to account for the L2, and L1 page for this L0
 *	- 1 is added to account for the control page of the map.
 */
#define BLKTOL0(b,s)      \
	(((((b) >> 23) << 10) + ((b) >> 23) + ((b) >> 33) + 2 + 1) << (s))

/*
 * convert disk block number to the logical block number of the LEVEL 1
 * dmapctl describing the disk block.  s is the log2(number of logical blocks
 * per page)
 *
 * The calculation figures out how many logical pages are in front of the L1.
 *	- the number of dmap pages preceding it
 *	- the number of L0 pages preceding it
 *	- the number of L1 pages preceding it
 *	- 1 is added to account for the L2 page
 *	- 1 is added to account for the control page of the map.
 */
#define BLKTOL1(b,s)      \
     (((((b) >> 33) << 20) + (((b) >> 33) << 10) + ((b) >> 33) + 1 + 1) << (s))

/*
 * convert disk block number to the logical block number of the dmapctl
 * at the specified level which describes the disk block.
 */
#define BLKTOCTL(b,s,l)   \
	(((l) == 2) ? 1 : ((l) == 1) ? BLKTOL1((b),(s)) : BLKTOL0((b),(s)))

/*
 * convert aggregate map size to the zero origin dmapctl level of the
 * top dmapctl.
 */
#define	BMAPSZTOLEV(size)	\
	(((size) <= MAXL0SIZE) ? 0 : ((size) <= MAXL1SIZE) ? 1 : 2)

/* convert disk block number to allocation group number.
 */
#define BLKTOAG(b,sbi)	((b) >> ((sbi)->bmap->db_agl2size))

/* convert allocation group number to starting disk block
 * number.
 */
#define AGTOBLK(a,ip)	\
	((s64)(a) << (JFS_SBI((ip)->i_sb)->bmap->db_agl2size))

/*
 *	dmap summary tree
 *
 * dmaptree must be consistent with dmapctl.
 */
struct dmaptree {
	__le32 nleafs;		/* 4: number of tree leafs	*/
	__le32 l2nleafs;	/* 4: l2 number of tree leafs	*/
	__le32 leafidx;		/* 4: index of first tree leaf	*/
	__le32 height;		/* 4: height of the tree	*/
	s8 budmin;		/* 1: min l2 tree leaf value to combine */
	s8 stree[TREESIZE];	/* TREESIZE: tree		*/
	u8 pad[2];		/* 2: pad to word boundary	*/
};				/* - 360 -			*/

/*
 *	dmap page per 8K blocks bitmap
 */
struct dmap {
	__le32 nblocks;		/* 4: num blks covered by this dmap	*/
	__le32 nfree;		/* 4: num of free blks in this dmap	*/
	__le64 start;		/* 8: starting blkno for this dmap	*/
	struct dmaptree tree;	/* 360: dmap tree			*/
	u8 pad[1672];		/* 1672: pad to 2048 bytes		*/
	__le32 wmap[LPERDMAP];	/* 1024: bits of the working map	*/
	__le32 pmap[LPERDMAP];	/* 1024: bits of the persistent map	*/
};				/* - 4096 -				*/

/*
 *	disk map control page per level.
 *
 * dmapctl must be consistent with dmaptree.
 */
struct dmapctl {
	__le32 nleafs;		/* 4: number of tree leafs	*/
	__le32 l2nleafs;	/* 4: l2 number of tree leafs	*/
	__le32 leafidx;		/* 4: index of the first tree leaf	*/
	__le32 height;		/* 4: height of tree		*/
	s8 budmin;		/* 1: minimum l2 tree leaf value	*/
	s8 stree[CTLTREESIZE];	/* CTLTREESIZE: dmapctl tree	*/
	u8 pad[2714];		/* 2714: pad to 4096		*/
};				/* - 4096 -			*/

/*
 *	common definition for dmaptree within dmap and dmapctl
 */
typedef union dmtree {
	struct dmaptree t1;
	struct dmapctl t2;
} dmtree_t;

/* macros for accessing fields within dmtree */
#define	dmt_nleafs	t1.nleafs
#define	dmt_l2nleafs	t1.l2nleafs
#define	dmt_leafidx	t1.leafidx
#define	dmt_height	t1.height
#define	dmt_budmin	t1.budmin
#define	dmt_stree	t1.stree

/*
 *	on-disk aggregate disk allocation map descriptor.
 */
struct dbmap_disk {
	__le64 dn_mapsize;	/* 8: number of blocks in aggregate	*/
	__le64 dn_nfree;	/* 8: num free blks in aggregate map	*/
	__le32 dn_l2nbperpage;	/* 4: number of blks per page		*/
	__le32 dn_numag;	/* 4: total number of ags		*/
	__le32 dn_maxlevel;	/* 4: number of active ags		*/
	__le32 dn_maxag;	/* 4: max active alloc group number	*/
	__le32 dn_agpref;	/* 4: preferred alloc group (hint)	*/
	__le32 dn_aglevel;	/* 4: dmapctl level holding the AG	*/
	__le32 dn_agheight;	/* 4: height in dmapctl of the AG	*/
	__le32 dn_agwidth;	/* 4: width in dmapctl of the AG	*/
	__le32 dn_agstart;	/* 4: start tree index at AG height	*/
	__le32 dn_agl2size;	/* 4: l2 num of blks per alloc group	*/
	__le64 dn_agfree[MAXAG];/* 8*MAXAG: per AG free count		*/
	__le64 dn_agsize;	/* 8: num of blks per alloc group	*/
	s8 dn_maxfreebud;	/* 1: max free buddy system		*/
	u8 pad[3007];		/* 3007: pad to 4096			*/
};				/* - 4096 -				*/

struct dbmap {
	s64 dn_mapsize;		/* number of blocks in aggregate	*/
	s64 dn_nfree;		/* num free blks in aggregate map	*/
	int dn_l2nbperpage;	/* number of blks per page		*/
	int dn_numag;		/* total number of ags			*/
	int dn_maxlevel;	/* number of active ags			*/
	int dn_maxag;		/* max active alloc group number	*/
	int dn_agpref;		/* preferred alloc group (hint)		*/
	int dn_aglevel;		/* dmapctl level holding the AG		*/
	int dn_agheight;	/* height in dmapctl of the AG		*/
	int dn_agwidth;		/* width in dmapctl of the AG		*/
	int dn_agstart;		/* start tree index at AG height	*/
	int dn_agl2size;	/* l2 num of blks per alloc group	*/
	s64 dn_agfree[MAXAG];	/* per AG free count			*/
	s64 dn_agsize;		/* num of blks per alloc group		*/
	signed char dn_maxfreebud;	/* max free buddy system	*/
};				/* - 4096 -				*/
/*
 *	in-memory aggregate disk allocation map descriptor.
 */
struct bmap {
	struct dbmap db_bmap;		/* on-disk aggregate map descriptor */
	struct inode *db_ipbmap;	/* ptr to aggregate map incore inode */
	struct mutex db_bmaplock;	/* aggregate map lock */
	atomic_t db_active[MAXAG];	/* count of active, open files in AG */
	u32 *db_DBmap;
};

/* macros for accessing fields within in-memory aggregate map descriptor */
#define	db_mapsize	db_bmap.dn_mapsize
#define	db_nfree	db_bmap.dn_nfree
#define	db_agfree	db_bmap.dn_agfree
#define	db_agsize	db_bmap.dn_agsize
#define	db_agl2size	db_bmap.dn_agl2size
#define	db_agwidth	db_bmap.dn_agwidth
#define	db_agheight	db_bmap.dn_agheight
#define	db_agstart	db_bmap.dn_agstart
#define	db_numag	db_bmap.dn_numag
#define	db_maxlevel	db_bmap.dn_maxlevel
#define	db_aglevel	db_bmap.dn_aglevel
#define	db_agpref	db_bmap.dn_agpref
#define	db_maxag	db_bmap.dn_maxag
#define	db_maxfreebud	db_bmap.dn_maxfreebud
#define	db_l2nbperpage	db_bmap.dn_l2nbperpage

/*
 * macros for various conversions needed by the allocators.
 * blkstol2(), cntlz(), and cnttz() are operating system dependent functions.
 */
/* convert number of blocks to log2 number of blocks, rounding up to
 * the next log2 value if blocks is not a l2 multiple.
 */
#define	BLKSTOL2(d)		(blkstol2(d))

/* convert number of leafs to log2 leaf value */
#define	NLSTOL2BSZ(n)		(31 - cntlz((n)) + BUDMIN)

/* convert leaf index to log2 leaf value */
#define	LITOL2BSZ(n,m,b)	((((n) == 0) ? (m) : cnttz((n))) + (b))

/* convert a block number to a dmap control leaf index */
#define BLKTOCTLLEAF(b,m)	\
	(((b) & (((s64)1 << ((m) + L2LPERCTL)) - 1)) >> (m))

/* convert log2 leaf value to buddy size */
#define	BUDSIZE(s,m)		(1 << ((s) - (m)))

/*
 *	external references.
 */
extern int dbMount(struct inode *ipbmap);

extern int dbUnmount(struct inode *ipbmap, int mounterror);

extern int dbFree(struct inode *ipbmap, s64 blkno, s64 nblocks);

extern int dbUpdatePMap(struct inode *ipbmap,
			int free, s64 blkno, s64 nblocks, struct tblock * tblk);

extern int dbNextAG(struct inode *ipbmap);

extern int dbAlloc(struct inode *ipbmap, s64 hint, s64 nblocks, s64 * results);

extern int dbReAlloc(struct inode *ipbmap,
		     s64 blkno, s64 nblocks, s64 addnblocks, s64 * results);

extern int dbSync(struct inode *ipbmap);
extern int dbAllocBottomUp(struct inode *ip, s64 blkno, s64 nblocks);
extern int dbExtendFS(struct inode *ipbmap, s64 blkno, s64 nblocks);
extern void dbFinalizeBmap(struct inode *ipbmap);
extern s64 dbMapFileSizeToMapSize(struct inode *ipbmap);
extern s64 dbDiscardAG(struct inode *ip, int agno, s64 minlen);

#endif				/* _H_JFS_DMAP */
