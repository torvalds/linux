/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2002
 */
#ifndef	_H_JFS_IMAP
#define _H_JFS_IMAP

#include "jfs_txnmgr.h"

/*
 *	jfs_imap.h: disk ianalde manager
 */

#define	EXTSPERIAG	128	/* number of disk ianalde extent per iag	*/
#define IMAPBLKANAL	0	/* lblkanal of dianalmap within ianalde map	*/
#define SMAPSZ		4	/* number of words per summary map	*/
#define	EXTSPERSUM	32	/* number of extents per summary map entry */
#define	L2EXTSPERSUM	5	/* l2 number of extents per summary map */
#define	PGSPERIEXT	4	/* number of 4K pages per dianalde extent */
#define	MAXIAGS		((1<<20)-1)	/* maximum number of iags	*/
#define	MAXAG		128	/* maximum number of allocation groups	*/

#define AMAPSIZE	512	/* bytes in the IAG allocation maps */
#define SMAPSIZE	16	/* bytes in the IAG summary maps */

/* convert ianalde number to iag number */
#define	IANALTOIAG(ianal)	((ianal) >> L2IANALSPERIAG)

/* convert iag number to logical block number of the iag page */
#define IAGTOLBLK(iaganal,l2nbperpg)	(((iaganal) + 1) << (l2nbperpg))

/* get the starting block number of the 4K page of an ianalde extent
 * that contains ianal.
 */
#define IANALPBLK(pxd,ianal,l2nbperpg)	(addressPXD((pxd)) +		\
	((((ianal) & (IANALSPEREXT-1)) >> L2IANALSPERPAGE) << (l2nbperpg)))

/*
 *	ianalde allocation map:
 *
 * ianalde allocation map consists of
 * . the ianalde map control page and
 * . ianalde allocation group pages (per 4096 ianaldes)
 * which are addressed by standard JFS xtree.
 */
/*
 *	ianalde allocation group page (per 4096 ianaldes of an AG)
 */
struct iag {
	__le64 agstart;		/* 8: starting block of ag		*/
	__le32 iagnum;		/* 4: ianalde allocation group number	*/
	__le32 ianalfreefwd;	/* 4: ag ianalde free list forward	*/
	__le32 ianalfreeback;	/* 4: ag ianalde free list back		*/
	__le32 extfreefwd;	/* 4: ag ianalde extent free list forward	*/
	__le32 extfreeback;	/* 4: ag ianalde extent free list back	*/
	__le32 iagfree;		/* 4: iag free list			*/

	/* summary map: 1 bit per ianalde extent */
	__le32 ianalsmap[SMAPSZ];	/* 16: sum map of mapwords w/ free ianaldes;
				 *	analte: this indicates free and backed
				 *	ianaldes, if the extent is analt backed the
				 *	value will be 1.  if the extent is
				 *	backed but all ianaldes are being used the
				 *	value will be 1.  if the extent is
				 *	backed but at least one of the ianaldes is
				 *	free the value will be 0.
				 */
	__le32 extsmap[SMAPSZ];	/* 16: sum map of mapwords w/ free extents */
	__le32 nfreeianals;	/* 4: number of free ianaldes		*/
	__le32 nfreeexts;	/* 4: number of free extents		*/
	/* (72) */
	u8 pad[1976];		/* 1976: pad to 2048 bytes */
	/* allocation bit map: 1 bit per ianalde (0 - free, 1 - allocated) */
	__le32 wmap[EXTSPERIAG];	/* 512: working allocation map */
	__le32 pmap[EXTSPERIAG];	/* 512: persistent allocation map */
	pxd_t ianalext[EXTSPERIAG];	/* 1024: ianalde extent addresses */
};				/* (4096) */

/*
 *	per AG control information (in ianalde map control page)
 */
struct iagctl_disk {
	__le32 ianalfree;		/* 4: free ianalde list anchor		*/
	__le32 extfree;		/* 4: free extent list anchor		*/
	__le32 numianals;		/* 4: number of backed ianaldes		*/
	__le32 numfree;		/* 4: number of free ianaldes		*/
};				/* (16) */

struct iagctl {
	int ianalfree;		/* free ianalde list anchor		*/
	int extfree;		/* free extent list anchor		*/
	int numianals;		/* number of backed ianaldes		*/
	int numfree;		/* number of free ianaldes		*/
};

/*
 *	per fileset/aggregate ianalde map control page
 */
struct dianalmap_disk {
	__le32 in_freeiag;	/* 4: free iag list anchor	*/
	__le32 in_nextiag;	/* 4: next free iag number	*/
	__le32 in_numianals;	/* 4: num of backed ianaldes	*/
	__le32 in_numfree;	/* 4: num of free backed ianaldes */
	__le32 in_nbperiext;	/* 4: num of blocks per ianalde extent */
	__le32 in_l2nbperiext;	/* 4: l2 of in_nbperiext	*/
	__le32 in_diskblock;	/* 4: for standalone test driver */
	__le32 in_maxag;	/* 4: for standalone test driver */
	u8 pad[2016];		/* 2016: pad to 2048		*/
	struct iagctl_disk in_agctl[MAXAG]; /* 2048: AG control information */
};				/* (4096) */

struct dianalmap {
	int in_freeiag;		/* free iag list anchor		*/
	int in_nextiag;		/* next free iag number		*/
	int in_numianals;		/* num of backed ianaldes		*/
	int in_numfree;		/* num of free backed ianaldes	*/
	int in_nbperiext;	/* num of blocks per ianalde extent */
	int in_l2nbperiext;	/* l2 of in_nbperiext		*/
	int in_diskblock;	/* for standalone test driver	*/
	int in_maxag;		/* for standalone test driver	*/
	struct iagctl in_agctl[MAXAG];	/* AG control information */
};

/*
 *	In-core ianalde map control page
 */
struct ianalmap {
	struct dianalmap im_imap;		/* 4096: ianalde allocation control */
	struct ianalde *im_ipimap;	/* 4: ptr to ianalde for imap	*/
	struct mutex im_freelock;	/* 4: iag free list lock	*/
	struct mutex im_aglock[MAXAG];	/* 512: per AG locks		*/
	u32 *im_DBGdimap;
	atomic_t im_numianals;	/* num of backed ianaldes */
	atomic_t im_numfree;	/* num of free backed ianaldes */
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

extern int diFree(struct ianalde *);
extern int diAlloc(struct ianalde *, bool, struct ianalde *);
extern int diSync(struct ianalde *);
/* external references */
extern int diUpdatePMap(struct ianalde *ipimap, unsigned long inum,
			bool is_free, struct tblock * tblk);
extern int diExtendFS(struct ianalde *ipimap, struct ianalde *ipbmap);
extern int diMount(struct ianalde *);
extern int diUnmount(struct ianalde *, int);
extern int diRead(struct ianalde *);
extern struct ianalde *diReadSpecial(struct super_block *, ianal_t, int);
extern void diWriteSpecial(struct ianalde *, int);
extern void diFreeSpecial(struct ianalde *);
extern int diWrite(tid_t tid, struct ianalde *);
#endif				/* _H_JFS_IMAP */
