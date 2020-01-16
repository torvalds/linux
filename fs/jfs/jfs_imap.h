/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2002
 */
#ifndef	_H_JFS_IMAP
#define _H_JFS_IMAP

#include "jfs_txnmgr.h"

/*
 *	jfs_imap.h: disk iyesde manager
 */

#define	EXTSPERIAG	128	/* number of disk iyesde extent per iag	*/
#define IMAPBLKNO	0	/* lblkyes of diyesmap within iyesde map	*/
#define SMAPSZ		4	/* number of words per summary map	*/
#define	EXTSPERSUM	32	/* number of extents per summary map entry */
#define	L2EXTSPERSUM	5	/* l2 number of extents per summary map */
#define	PGSPERIEXT	4	/* number of 4K pages per diyesde extent */
#define	MAXIAGS		((1<<20)-1)	/* maximum number of iags	*/
#define	MAXAG		128	/* maximum number of allocation groups	*/

#define AMAPSIZE	512	/* bytes in the IAG allocation maps */
#define SMAPSIZE	16	/* bytes in the IAG summary maps */

/* convert iyesde number to iag number */
#define	INOTOIAG(iyes)	((iyes) >> L2INOSPERIAG)

/* convert iag number to logical block number of the iag page */
#define IAGTOLBLK(iagyes,l2nbperpg)	(((iagyes) + 1) << (l2nbperpg))

/* get the starting block number of the 4K page of an iyesde extent
 * that contains iyes.
 */
#define INOPBLK(pxd,iyes,l2nbperpg)	(addressPXD((pxd)) +		\
	((((iyes) & (INOSPEREXT-1)) >> L2INOSPERPAGE) << (l2nbperpg)))

/*
 *	iyesde allocation map:
 *
 * iyesde allocation map consists of
 * . the iyesde map control page and
 * . iyesde allocation group pages (per 4096 iyesdes)
 * which are addressed by standard JFS xtree.
 */
/*
 *	iyesde allocation group page (per 4096 iyesdes of an AG)
 */
struct iag {
	__le64 agstart;		/* 8: starting block of ag		*/
	__le32 iagnum;		/* 4: iyesde allocation group number	*/
	__le32 iyesfreefwd;	/* 4: ag iyesde free list forward	*/
	__le32 iyesfreeback;	/* 4: ag iyesde free list back		*/
	__le32 extfreefwd;	/* 4: ag iyesde extent free list forward	*/
	__le32 extfreeback;	/* 4: ag iyesde extent free list back	*/
	__le32 iagfree;		/* 4: iag free list			*/

	/* summary map: 1 bit per iyesde extent */
	__le32 iyessmap[SMAPSZ];	/* 16: sum map of mapwords w/ free iyesdes;
				 *	yeste: this indicates free and backed
				 *	iyesdes, if the extent is yest backed the
				 *	value will be 1.  if the extent is
				 *	backed but all iyesdes are being used the
				 *	value will be 1.  if the extent is
				 *	backed but at least one of the iyesdes is
				 *	free the value will be 0.
				 */
	__le32 extsmap[SMAPSZ];	/* 16: sum map of mapwords w/ free extents */
	__le32 nfreeiyess;	/* 4: number of free iyesdes		*/
	__le32 nfreeexts;	/* 4: number of free extents		*/
	/* (72) */
	u8 pad[1976];		/* 1976: pad to 2048 bytes */
	/* allocation bit map: 1 bit per iyesde (0 - free, 1 - allocated) */
	__le32 wmap[EXTSPERIAG];	/* 512: working allocation map */
	__le32 pmap[EXTSPERIAG];	/* 512: persistent allocation map */
	pxd_t iyesext[EXTSPERIAG];	/* 1024: iyesde extent addresses */
};				/* (4096) */

/*
 *	per AG control information (in iyesde map control page)
 */
struct iagctl_disk {
	__le32 iyesfree;		/* 4: free iyesde list anchor		*/
	__le32 extfree;		/* 4: free extent list anchor		*/
	__le32 numiyess;		/* 4: number of backed iyesdes		*/
	__le32 numfree;		/* 4: number of free iyesdes		*/
};				/* (16) */

struct iagctl {
	int iyesfree;		/* free iyesde list anchor		*/
	int extfree;		/* free extent list anchor		*/
	int numiyess;		/* number of backed iyesdes		*/
	int numfree;		/* number of free iyesdes		*/
};

/*
 *	per fileset/aggregate iyesde map control page
 */
struct diyesmap_disk {
	__le32 in_freeiag;	/* 4: free iag list anchor	*/
	__le32 in_nextiag;	/* 4: next free iag number	*/
	__le32 in_numiyess;	/* 4: num of backed iyesdes	*/
	__le32 in_numfree;	/* 4: num of free backed iyesdes */
	__le32 in_nbperiext;	/* 4: num of blocks per iyesde extent */
	__le32 in_l2nbperiext;	/* 4: l2 of in_nbperiext	*/
	__le32 in_diskblock;	/* 4: for standalone test driver */
	__le32 in_maxag;	/* 4: for standalone test driver */
	u8 pad[2016];		/* 2016: pad to 2048		*/
	struct iagctl_disk in_agctl[MAXAG]; /* 2048: AG control information */
};				/* (4096) */

struct diyesmap {
	int in_freeiag;		/* free iag list anchor		*/
	int in_nextiag;		/* next free iag number		*/
	int in_numiyess;		/* num of backed iyesdes		*/
	int in_numfree;		/* num of free backed iyesdes	*/
	int in_nbperiext;	/* num of blocks per iyesde extent */
	int in_l2nbperiext;	/* l2 of in_nbperiext		*/
	int in_diskblock;	/* for standalone test driver	*/
	int in_maxag;		/* for standalone test driver	*/
	struct iagctl in_agctl[MAXAG];	/* AG control information */
};

/*
 *	In-core iyesde map control page
 */
struct iyesmap {
	struct diyesmap im_imap;		/* 4096: iyesde allocation control */
	struct iyesde *im_ipimap;	/* 4: ptr to iyesde for imap	*/
	struct mutex im_freelock;	/* 4: iag free list lock	*/
	struct mutex im_aglock[MAXAG];	/* 512: per AG locks		*/
	u32 *im_DBGdimap;
	atomic_t im_numiyess;	/* num of backed iyesdes */
	atomic_t im_numfree;	/* num of free backed iyesdes */
};

#define	im_freeiag	im_imap.in_freeiag
#define	im_nextiag	im_imap.in_nextiag
#define	im_agctl	im_imap.in_agctl
#define	im_nbperiext	im_imap.in_nbperiext
#define	im_l2nbperiext	im_imap.in_l2nbperiext

/* for standalone testdriver
 */
#define	im_diskblock	im_imap.in_diskblock
#define	im_maxag	im_imap.in_maxag

extern int diFree(struct iyesde *);
extern int diAlloc(struct iyesde *, bool, struct iyesde *);
extern int diSync(struct iyesde *);
/* external references */
extern int diUpdatePMap(struct iyesde *ipimap, unsigned long inum,
			bool is_free, struct tblock * tblk);
extern int diExtendFS(struct iyesde *ipimap, struct iyesde *ipbmap);
extern int diMount(struct iyesde *);
extern int diUnmount(struct iyesde *, int);
extern int diRead(struct iyesde *);
extern struct iyesde *diReadSpecial(struct super_block *, iyes_t, int);
extern void diWriteSpecial(struct iyesde *, int);
extern void diFreeSpecial(struct iyesde *);
extern int diWrite(tid_t tid, struct iyesde *);
#endif				/* _H_JFS_IMAP */
