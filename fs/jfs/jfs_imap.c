// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 */

/*
 *	jfs_imap.c: iyesde allocation map manager
 *
 * Serialization:
 *   Each AG has a simple lock which is used to control the serialization of
 *	the AG level lists.  This lock should be taken first whenever an AG
 *	level list will be modified or accessed.
 *
 *   Each IAG is locked by obtaining the buffer for the IAG page.
 *
 *   There is also a iyesde lock for the iyesde map iyesde.  A read lock needs to
 *	be taken whenever an IAG is read from the map or the global level
 *	information is read.  A write lock needs to be taken whenever the global
 *	level information is modified or an atomic operation needs to be used.
 *
 *	If more than one IAG is read at one time, the read lock may yest
 *	be given up until all of the IAG's are read.  Otherwise, a deadlock
 *	may occur when trying to obtain the read lock while ayesther thread
 *	holding the read lock is waiting on the IAG already being held.
 *
 *   The control page of the iyesde map is read into memory by diMount().
 *	Thereafter it should only be modified in memory and then it will be
 *	written out when the filesystem is unmounted by diUnmount().
 */

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/slab.h>

#include "jfs_incore.h"
#include "jfs_iyesde.h"
#include "jfs_filsys.h"
#include "jfs_diyesde.h"
#include "jfs_dmap.h"
#include "jfs_imap.h"
#include "jfs_metapage.h"
#include "jfs_superblock.h"
#include "jfs_debug.h"

/*
 * imap locks
 */
/* iag free list lock */
#define IAGFREE_LOCK_INIT(imap)		mutex_init(&imap->im_freelock)
#define IAGFREE_LOCK(imap)		mutex_lock(&imap->im_freelock)
#define IAGFREE_UNLOCK(imap)		mutex_unlock(&imap->im_freelock)

/* per ag iag list locks */
#define AG_LOCK_INIT(imap,index)	mutex_init(&(imap->im_aglock[index]))
#define AG_LOCK(imap,agyes)		mutex_lock(&imap->im_aglock[agyes])
#define AG_UNLOCK(imap,agyes)		mutex_unlock(&imap->im_aglock[agyes])

/*
 * forward references
 */
static int diAllocAG(struct iyesmap *, int, bool, struct iyesde *);
static int diAllocAny(struct iyesmap *, int, bool, struct iyesde *);
static int diAllocBit(struct iyesmap *, struct iag *, int);
static int diAllocExt(struct iyesmap *, int, struct iyesde *);
static int diAllocIyes(struct iyesmap *, int, struct iyesde *);
static int diFindFree(u32, int);
static int diNewExt(struct iyesmap *, struct iag *, int);
static int diNewIAG(struct iyesmap *, int *, int, struct metapage **);
static void duplicateIXtree(struct super_block *, s64, int, s64 *);

static int diIAGRead(struct iyesmap * imap, int, struct metapage **);
static int copy_from_diyesde(struct diyesde *, struct iyesde *);
static void copy_to_diyesde(struct diyesde *, struct iyesde *);

/*
 * NAME:	diMount()
 *
 * FUNCTION:	initialize the incore iyesde map control structures for
 *		a fileset or aggregate init time.
 *
 *		the iyesde map's control structure (diyesmap) is
 *		brought in from disk and placed in virtual memory.
 *
 * PARAMETERS:
 *	ipimap	- pointer to iyesde map iyesde for the aggregate or fileset.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOMEM	- insufficient free virtual memory.
 *	-EIO	- i/o error.
 */
int diMount(struct iyesde *ipimap)
{
	struct iyesmap *imap;
	struct metapage *mp;
	int index;
	struct diyesmap_disk *diyesm_le;

	/*
	 * allocate/initialize the in-memory iyesde map control structure
	 */
	/* allocate the in-memory iyesde map control structure. */
	imap = kmalloc(sizeof(struct iyesmap), GFP_KERNEL);
	if (imap == NULL) {
		jfs_err("diMount: kmalloc returned NULL!");
		return -ENOMEM;
	}

	/* read the on-disk iyesde map control structure. */

	mp = read_metapage(ipimap,
			   IMAPBLKNO << JFS_SBI(ipimap->i_sb)->l2nbperpage,
			   PSIZE, 0);
	if (mp == NULL) {
		kfree(imap);
		return -EIO;
	}

	/* copy the on-disk version to the in-memory version. */
	diyesm_le = (struct diyesmap_disk *) mp->data;
	imap->im_freeiag = le32_to_cpu(diyesm_le->in_freeiag);
	imap->im_nextiag = le32_to_cpu(diyesm_le->in_nextiag);
	atomic_set(&imap->im_numiyess, le32_to_cpu(diyesm_le->in_numiyess));
	atomic_set(&imap->im_numfree, le32_to_cpu(diyesm_le->in_numfree));
	imap->im_nbperiext = le32_to_cpu(diyesm_le->in_nbperiext);
	imap->im_l2nbperiext = le32_to_cpu(diyesm_le->in_l2nbperiext);
	for (index = 0; index < MAXAG; index++) {
		imap->im_agctl[index].iyesfree =
		    le32_to_cpu(diyesm_le->in_agctl[index].iyesfree);
		imap->im_agctl[index].extfree =
		    le32_to_cpu(diyesm_le->in_agctl[index].extfree);
		imap->im_agctl[index].numiyess =
		    le32_to_cpu(diyesm_le->in_agctl[index].numiyess);
		imap->im_agctl[index].numfree =
		    le32_to_cpu(diyesm_le->in_agctl[index].numfree);
	}

	/* release the buffer. */
	release_metapage(mp);

	/*
	 * allocate/initialize iyesde allocation map locks
	 */
	/* allocate and init iag free list lock */
	IAGFREE_LOCK_INIT(imap);

	/* allocate and init ag list locks */
	for (index = 0; index < MAXAG; index++) {
		AG_LOCK_INIT(imap, index);
	}

	/* bind the iyesde map iyesde and iyesde map control structure
	 * to each other.
	 */
	imap->im_ipimap = ipimap;
	JFS_IP(ipimap)->i_imap = imap;

	return (0);
}


/*
 * NAME:	diUnmount()
 *
 * FUNCTION:	write to disk the incore iyesde map control structures for
 *		a fileset or aggregate at unmount time.
 *
 * PARAMETERS:
 *	ipimap	- pointer to iyesde map iyesde for the aggregate or fileset.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOMEM	- insufficient free virtual memory.
 *	-EIO	- i/o error.
 */
int diUnmount(struct iyesde *ipimap, int mounterror)
{
	struct iyesmap *imap = JFS_IP(ipimap)->i_imap;

	/*
	 * update the on-disk iyesde map control structure
	 */

	if (!(mounterror || isReadOnly(ipimap)))
		diSync(ipimap);

	/*
	 * Invalidate the page cache buffers
	 */
	truncate_iyesde_pages(ipimap->i_mapping, 0);

	/*
	 * free in-memory control structure
	 */
	kfree(imap);

	return (0);
}


/*
 *	diSync()
 */
int diSync(struct iyesde *ipimap)
{
	struct diyesmap_disk *diyesm_le;
	struct iyesmap *imp = JFS_IP(ipimap)->i_imap;
	struct metapage *mp;
	int index;

	/*
	 * write imap global conrol page
	 */
	/* read the on-disk iyesde map control structure */
	mp = get_metapage(ipimap,
			  IMAPBLKNO << JFS_SBI(ipimap->i_sb)->l2nbperpage,
			  PSIZE, 0);
	if (mp == NULL) {
		jfs_err("diSync: get_metapage failed!");
		return -EIO;
	}

	/* copy the in-memory version to the on-disk version */
	diyesm_le = (struct diyesmap_disk *) mp->data;
	diyesm_le->in_freeiag = cpu_to_le32(imp->im_freeiag);
	diyesm_le->in_nextiag = cpu_to_le32(imp->im_nextiag);
	diyesm_le->in_numiyess = cpu_to_le32(atomic_read(&imp->im_numiyess));
	diyesm_le->in_numfree = cpu_to_le32(atomic_read(&imp->im_numfree));
	diyesm_le->in_nbperiext = cpu_to_le32(imp->im_nbperiext);
	diyesm_le->in_l2nbperiext = cpu_to_le32(imp->im_l2nbperiext);
	for (index = 0; index < MAXAG; index++) {
		diyesm_le->in_agctl[index].iyesfree =
		    cpu_to_le32(imp->im_agctl[index].iyesfree);
		diyesm_le->in_agctl[index].extfree =
		    cpu_to_le32(imp->im_agctl[index].extfree);
		diyesm_le->in_agctl[index].numiyess =
		    cpu_to_le32(imp->im_agctl[index].numiyess);
		diyesm_le->in_agctl[index].numfree =
		    cpu_to_le32(imp->im_agctl[index].numfree);
	}

	/* write out the control structure */
	write_metapage(mp);

	/*
	 * write out dirty pages of imap
	 */
	filemap_write_and_wait(ipimap->i_mapping);

	diWriteSpecial(ipimap, 0);

	return (0);
}


/*
 * NAME:	diRead()
 *
 * FUNCTION:	initialize an incore iyesde from disk.
 *
 *		on entry, the specifed incore iyesde should itself
 *		specify the disk iyesde number corresponding to the
 *		incore iyesde (i.e. i_number should be initialized).
 *
 *		this routine handles incore iyesde initialization for
 *		both "special" and "regular" iyesdes.  special iyesdes
 *		are those required early in the mount process and
 *		require special handling since much of the file system
 *		is yest yet initialized.  these "special" iyesdes are
 *		identified by a NULL iyesde map iyesde pointer and are
 *		actually initialized by a call to diReadSpecial().
 *
 *		for regular iyesdes, the iag describing the disk iyesde
 *		is read from disk to determine the iyesde extent address
 *		for the disk iyesde.  with the iyesde extent address in
 *		hand, the page of the extent that contains the disk
 *		iyesde is read and the disk iyesde is copied to the
 *		incore iyesde.
 *
 * PARAMETERS:
 *	ip	-  pointer to incore iyesde to be initialized from disk.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 *	-ENOMEM	- insufficient memory
 *
 */
int diRead(struct iyesde *ip)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	int iagyes, iyes, extyes, rc;
	struct iyesde *ipimap;
	struct diyesde *dp;
	struct iag *iagp;
	struct metapage *mp;
	s64 blkyes, agstart;
	struct iyesmap *imap;
	int block_offset;
	int iyesdes_left;
	unsigned long pageyes;
	int rel_iyesde;

	jfs_info("diRead: iyes = %ld", ip->i_iyes);

	ipimap = sbi->ipimap;
	JFS_IP(ip)->ipimap = ipimap;

	/* determine the iag number for this iyesde (number) */
	iagyes = INOTOIAG(ip->i_iyes);

	/* read the iag */
	imap = JFS_IP(ipimap)->i_imap;
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);
	rc = diIAGRead(imap, iagyes, &mp);
	IREAD_UNLOCK(ipimap);
	if (rc) {
		jfs_err("diRead: diIAGRead returned %d", rc);
		return (rc);
	}

	iagp = (struct iag *) mp->data;

	/* determine iyesde extent that holds the disk iyesde */
	iyes = ip->i_iyes & (INOSPERIAG - 1);
	extyes = iyes >> L2INOSPEREXT;

	if ((lengthPXD(&iagp->iyesext[extyes]) != imap->im_nbperiext) ||
	    (addressPXD(&iagp->iyesext[extyes]) == 0)) {
		release_metapage(mp);
		return -ESTALE;
	}

	/* get disk block number of the page within the iyesde extent
	 * that holds the disk iyesde.
	 */
	blkyes = INOPBLK(&iagp->iyesext[extyes], iyes, sbi->l2nbperpage);

	/* get the ag for the iag */
	agstart = le64_to_cpu(iagp->agstart);

	release_metapage(mp);

	rel_iyesde = (iyes & (INOSPERPAGE - 1));
	pageyes = blkyes >> sbi->l2nbperpage;

	if ((block_offset = ((u32) blkyes & (sbi->nbperpage - 1)))) {
		/*
		 * OS/2 didn't always align iyesde extents on page boundaries
		 */
		iyesdes_left =
		     (sbi->nbperpage - block_offset) << sbi->l2niperblk;

		if (rel_iyesde < iyesdes_left)
			rel_iyesde += block_offset << sbi->l2niperblk;
		else {
			pageyes += 1;
			rel_iyesde -= iyesdes_left;
		}
	}

	/* read the page of disk iyesde */
	mp = read_metapage(ipimap, pageyes << sbi->l2nbperpage, PSIZE, 1);
	if (!mp) {
		jfs_err("diRead: read_metapage failed");
		return -EIO;
	}

	/* locate the disk iyesde requested */
	dp = (struct diyesde *) mp->data;
	dp += rel_iyesde;

	if (ip->i_iyes != le32_to_cpu(dp->di_number)) {
		jfs_error(ip->i_sb, "i_iyes != di_number\n");
		rc = -EIO;
	} else if (le32_to_cpu(dp->di_nlink) == 0)
		rc = -ESTALE;
	else
		/* copy the disk iyesde to the in-memory iyesde */
		rc = copy_from_diyesde(dp, ip);

	release_metapage(mp);

	/* set the ag for the iyesde */
	JFS_IP(ip)->agstart = agstart;
	JFS_IP(ip)->active_ag = -1;

	return (rc);
}


/*
 * NAME:	diReadSpecial()
 *
 * FUNCTION:	initialize a 'special' iyesde from disk.
 *
 *		this routines handles aggregate level iyesdes.  The
 *		iyesde cache canyest differentiate between the
 *		aggregate iyesdes and the filesystem iyesdes, so we
 *		handle these here.  We don't actually use the aggregate
 *		iyesde map, since these iyesdes are at a fixed location
 *		and in some cases the aggregate iyesde map isn't initialized
 *		yet.
 *
 * PARAMETERS:
 *	sb - filesystem superblock
 *	inum - aggregate iyesde number
 *	secondary - 1 if secondary aggregate iyesde table
 *
 * RETURN VALUES:
 *	new iyesde	- success
 *	NULL		- i/o error.
 */
struct iyesde *diReadSpecial(struct super_block *sb, iyes_t inum, int secondary)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	uint address;
	struct diyesde *dp;
	struct iyesde *ip;
	struct metapage *mp;

	ip = new_iyesde(sb);
	if (ip == NULL) {
		jfs_err("diReadSpecial: new_iyesde returned NULL!");
		return ip;
	}

	if (secondary) {
		address = addressPXD(&sbi->ait2) >> sbi->l2nbperpage;
		JFS_IP(ip)->ipimap = sbi->ipaimap2;
	} else {
		address = AITBL_OFF >> L2PSIZE;
		JFS_IP(ip)->ipimap = sbi->ipaimap;
	}

	ASSERT(inum < INOSPEREXT);

	ip->i_iyes = inum;

	address += inum >> 3;	/* 8 iyesdes per 4K page */

	/* read the page of fixed disk iyesde (AIT) in raw mode */
	mp = read_metapage(ip, address << sbi->l2nbperpage, PSIZE, 1);
	if (mp == NULL) {
		set_nlink(ip, 1);	/* Don't want iput() deleting it */
		iput(ip);
		return (NULL);
	}

	/* get the pointer to the disk iyesde of interest */
	dp = (struct diyesde *) (mp->data);
	dp += inum % 8;		/* 8 iyesdes per 4K page */

	/* copy on-disk iyesde to in-memory iyesde */
	if ((copy_from_diyesde(dp, ip)) != 0) {
		/* handle bad return by returning NULL for ip */
		set_nlink(ip, 1);	/* Don't want iput() deleting it */
		iput(ip);
		/* release the page */
		release_metapage(mp);
		return (NULL);

	}

	ip->i_mapping->a_ops = &jfs_metapage_aops;
	mapping_set_gfp_mask(ip->i_mapping, GFP_NOFS);

	/* Allocations to metadata iyesdes should yest affect quotas */
	ip->i_flags |= S_NOQUOTA;

	if ((inum == FILESYSTEM_I) && (JFS_IP(ip)->ipimap == sbi->ipaimap)) {
		sbi->gengen = le32_to_cpu(dp->di_gengen);
		sbi->iyesstamp = le32_to_cpu(dp->di_iyesstamp);
	}

	/* release the page */
	release_metapage(mp);

	iyesde_fake_hash(ip);

	return (ip);
}

/*
 * NAME:	diWriteSpecial()
 *
 * FUNCTION:	Write the special iyesde to disk
 *
 * PARAMETERS:
 *	ip - special iyesde
 *	secondary - 1 if secondary aggregate iyesde table
 *
 * RETURN VALUES: yesne
 */

void diWriteSpecial(struct iyesde *ip, int secondary)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	uint address;
	struct diyesde *dp;
	iyes_t inum = ip->i_iyes;
	struct metapage *mp;

	if (secondary)
		address = addressPXD(&sbi->ait2) >> sbi->l2nbperpage;
	else
		address = AITBL_OFF >> L2PSIZE;

	ASSERT(inum < INOSPEREXT);

	address += inum >> 3;	/* 8 iyesdes per 4K page */

	/* read the page of fixed disk iyesde (AIT) in raw mode */
	mp = read_metapage(ip, address << sbi->l2nbperpage, PSIZE, 1);
	if (mp == NULL) {
		jfs_err("diWriteSpecial: failed to read aggregate iyesde extent!");
		return;
	}

	/* get the pointer to the disk iyesde of interest */
	dp = (struct diyesde *) (mp->data);
	dp += inum % 8;		/* 8 iyesdes per 4K page */

	/* copy on-disk iyesde to in-memory iyesde */
	copy_to_diyesde(dp, ip);
	memcpy(&dp->di_xtroot, &JFS_IP(ip)->i_xtroot, 288);

	if (inum == FILESYSTEM_I)
		dp->di_gengen = cpu_to_le32(sbi->gengen);

	/* write the page */
	write_metapage(mp);
}

/*
 * NAME:	diFreeSpecial()
 *
 * FUNCTION:	Free allocated space for special iyesde
 */
void diFreeSpecial(struct iyesde *ip)
{
	if (ip == NULL) {
		jfs_err("diFreeSpecial called with NULL ip!");
		return;
	}
	filemap_write_and_wait(ip->i_mapping);
	truncate_iyesde_pages(ip->i_mapping, 0);
	iput(ip);
}



/*
 * NAME:	diWrite()
 *
 * FUNCTION:	write the on-disk iyesde portion of the in-memory iyesde
 *		to its corresponding on-disk iyesde.
 *
 *		on entry, the specifed incore iyesde should itself
 *		specify the disk iyesde number corresponding to the
 *		incore iyesde (i.e. i_number should be initialized).
 *
 *		the iyesde contains the iyesde extent address for the disk
 *		iyesde.  with the iyesde extent address in hand, the
 *		page of the extent that contains the disk iyesde is
 *		read and the disk iyesde portion of the incore iyesde
 *		is copied to the disk iyesde.
 *
 * PARAMETERS:
 *	tid -  transacation id
 *	ip  -  pointer to incore iyesde to be written to the iyesde extent.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 */
int diWrite(tid_t tid, struct iyesde *ip)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	struct jfs_iyesde_info *jfs_ip = JFS_IP(ip);
	int rc = 0;
	s32 iyes;
	struct diyesde *dp;
	s64 blkyes;
	int block_offset;
	int iyesdes_left;
	struct metapage *mp;
	unsigned long pageyes;
	int rel_iyesde;
	int dioffset;
	struct iyesde *ipimap;
	uint type;
	lid_t lid;
	struct tlock *ditlck, *tlck;
	struct linelock *dilinelock, *ilinelock;
	struct lv *lv;
	int n;

	ipimap = jfs_ip->ipimap;

	iyes = ip->i_iyes & (INOSPERIAG - 1);

	if (!addressPXD(&(jfs_ip->ixpxd)) ||
	    (lengthPXD(&(jfs_ip->ixpxd)) !=
	     JFS_IP(ipimap)->i_imap->im_nbperiext)) {
		jfs_error(ip->i_sb, "ixpxd invalid\n");
		return -EIO;
	}

	/*
	 * read the page of disk iyesde containing the specified iyesde:
	 */
	/* compute the block address of the page */
	blkyes = INOPBLK(&(jfs_ip->ixpxd), iyes, sbi->l2nbperpage);

	rel_iyesde = (iyes & (INOSPERPAGE - 1));
	pageyes = blkyes >> sbi->l2nbperpage;

	if ((block_offset = ((u32) blkyes & (sbi->nbperpage - 1)))) {
		/*
		 * OS/2 didn't always align iyesde extents on page boundaries
		 */
		iyesdes_left =
		    (sbi->nbperpage - block_offset) << sbi->l2niperblk;

		if (rel_iyesde < iyesdes_left)
			rel_iyesde += block_offset << sbi->l2niperblk;
		else {
			pageyes += 1;
			rel_iyesde -= iyesdes_left;
		}
	}
	/* read the page of disk iyesde */
      retry:
	mp = read_metapage(ipimap, pageyes << sbi->l2nbperpage, PSIZE, 1);
	if (!mp)
		return -EIO;

	/* get the pointer to the disk iyesde */
	dp = (struct diyesde *) mp->data;
	dp += rel_iyesde;

	dioffset = (iyes & (INOSPERPAGE - 1)) << L2DISIZE;

	/*
	 * acquire transaction lock on the on-disk iyesde;
	 * N.B. tlock is acquired on ipimap yest ip;
	 */
	if ((ditlck =
	     txLock(tid, ipimap, mp, tlckINODE | tlckENTRY)) == NULL)
		goto retry;
	dilinelock = (struct linelock *) & ditlck->lock;

	/*
	 * copy btree root from in-memory iyesde to on-disk iyesde
	 *
	 * (tlock is taken from inline B+-tree root in in-memory
	 * iyesde when the B+-tree root is updated, which is pointed
	 * by jfs_ip->blid as well as being on tx tlock list)
	 *
	 * further processing of btree root is based on the copy
	 * in in-memory iyesde, where txLog() will log from, and,
	 * for xtree root, txUpdateMap() will update map and reset
	 * XAD_NEW bit;
	 */

	if (S_ISDIR(ip->i_mode) && (lid = jfs_ip->xtlid)) {
		/*
		 * This is the special xtree inside the directory for storing
		 * the directory table
		 */
		xtpage_t *p, *xp;
		xad_t *xad;

		jfs_ip->xtlid = 0;
		tlck = lid_to_tlock(lid);
		assert(tlck->type & tlckXTREE);
		tlck->type |= tlckBTROOT;
		tlck->mp = mp;
		ilinelock = (struct linelock *) & tlck->lock;

		/*
		 * copy xtree root from iyesde to diyesde:
		 */
		p = &jfs_ip->i_xtroot;
		xp = (xtpage_t *) &dp->di_dirtable;
		lv = ilinelock->lv;
		for (n = 0; n < ilinelock->index; n++, lv++) {
			memcpy(&xp->xad[lv->offset], &p->xad[lv->offset],
			       lv->length << L2XTSLOTSIZE);
		}

		/* reset on-disk (metadata page) xtree XAD_NEW bit */
		xad = &xp->xad[XTENTRYSTART];
		for (n = XTENTRYSTART;
		     n < le16_to_cpu(xp->header.nextindex); n++, xad++)
			if (xad->flag & (XAD_NEW | XAD_EXTENDED))
				xad->flag &= ~(XAD_NEW | XAD_EXTENDED);
	}

	if ((lid = jfs_ip->blid) == 0)
		goto inlineData;
	jfs_ip->blid = 0;

	tlck = lid_to_tlock(lid);
	type = tlck->type;
	tlck->type |= tlckBTROOT;
	tlck->mp = mp;
	ilinelock = (struct linelock *) & tlck->lock;

	/*
	 *	regular file: 16 byte (XAD slot) granularity
	 */
	if (type & tlckXTREE) {
		xtpage_t *p, *xp;
		xad_t *xad;

		/*
		 * copy xtree root from iyesde to diyesde:
		 */
		p = &jfs_ip->i_xtroot;
		xp = &dp->di_xtroot;
		lv = ilinelock->lv;
		for (n = 0; n < ilinelock->index; n++, lv++) {
			memcpy(&xp->xad[lv->offset], &p->xad[lv->offset],
			       lv->length << L2XTSLOTSIZE);
		}

		/* reset on-disk (metadata page) xtree XAD_NEW bit */
		xad = &xp->xad[XTENTRYSTART];
		for (n = XTENTRYSTART;
		     n < le16_to_cpu(xp->header.nextindex); n++, xad++)
			if (xad->flag & (XAD_NEW | XAD_EXTENDED))
				xad->flag &= ~(XAD_NEW | XAD_EXTENDED);
	}
	/*
	 *	directory: 32 byte (directory entry slot) granularity
	 */
	else if (type & tlckDTREE) {
		dtpage_t *p, *xp;

		/*
		 * copy dtree root from iyesde to diyesde:
		 */
		p = (dtpage_t *) &jfs_ip->i_dtroot;
		xp = (dtpage_t *) & dp->di_dtroot;
		lv = ilinelock->lv;
		for (n = 0; n < ilinelock->index; n++, lv++) {
			memcpy(&xp->slot[lv->offset], &p->slot[lv->offset],
			       lv->length << L2DTSLOTSIZE);
		}
	} else {
		jfs_err("diWrite: UFO tlock");
	}

      inlineData:
	/*
	 * copy inline symlink from in-memory iyesde to on-disk iyesde
	 */
	if (S_ISLNK(ip->i_mode) && ip->i_size < IDATASIZE) {
		lv = & dilinelock->lv[dilinelock->index];
		lv->offset = (dioffset + 2 * 128) >> L2INODESLOTSIZE;
		lv->length = 2;
		memcpy(&dp->di_fastsymlink, jfs_ip->i_inline, IDATASIZE);
		dilinelock->index++;
	}
	/*
	 * copy inline data from in-memory iyesde to on-disk iyesde:
	 * 128 byte slot granularity
	 */
	if (test_cflag(COMMIT_Inlineea, ip)) {
		lv = & dilinelock->lv[dilinelock->index];
		lv->offset = (dioffset + 3 * 128) >> L2INODESLOTSIZE;
		lv->length = 1;
		memcpy(&dp->di_inlineea, jfs_ip->i_inline_ea, INODESLOTSIZE);
		dilinelock->index++;

		clear_cflag(COMMIT_Inlineea, ip);
	}

	/*
	 *	lock/copy iyesde base: 128 byte slot granularity
	 */
	lv = & dilinelock->lv[dilinelock->index];
	lv->offset = dioffset >> L2INODESLOTSIZE;
	copy_to_diyesde(dp, ip);
	if (test_and_clear_cflag(COMMIT_Dirtable, ip)) {
		lv->length = 2;
		memcpy(&dp->di_dirtable, &jfs_ip->i_dirtable, 96);
	} else
		lv->length = 1;
	dilinelock->index++;

	/* release the buffer holding the updated on-disk iyesde.
	 * the buffer will be later written by commit processing.
	 */
	write_metapage(mp);

	return (rc);
}


/*
 * NAME:	diFree(ip)
 *
 * FUNCTION:	free a specified iyesde from the iyesde working map
 *		for a fileset or aggregate.
 *
 *		if the iyesde to be freed represents the first (only)
 *		free iyesde within the iag, the iag will be placed on
 *		the ag free iyesde list.
 *
 *		freeing the iyesde will cause the iyesde extent to be
 *		freed if the iyesde is the only allocated iyesde within
 *		the extent.  in this case all the disk resource backing
 *		up the iyesde extent will be freed. in addition, the iag
 *		will be placed on the ag extent free list if the extent
 *		is the first free extent in the iag.  if freeing the
 *		extent also means that yes free iyesdes will exist for
 *		the iag, the iag will also be removed from the ag free
 *		iyesde list.
 *
 *		the iag describing the iyesde will be freed if the extent
 *		is to be freed and it is the only backed extent within
 *		the iag.  in this case, the iag will be removed from the
 *		ag free extent list and ag free iyesde list and placed on
 *		the iyesde map's free iag list.
 *
 *		a careful update approach is used to provide consistency
 *		in the face of updates to multiple buffers.  under this
 *		approach, all required buffers are obtained before making
 *		any updates and are held until all updates are complete.
 *
 * PARAMETERS:
 *	ip	- iyesde to be freed.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 */
int diFree(struct iyesde *ip)
{
	int rc;
	iyes_t inum = ip->i_iyes;
	struct iag *iagp, *aiagp, *biagp, *ciagp, *diagp;
	struct metapage *mp, *amp, *bmp, *cmp, *dmp;
	int iagyes, iyes, extyes, bityes, sword, agyes;
	int back, fwd;
	u32 bitmap, mask;
	struct iyesde *ipimap = JFS_SBI(ip->i_sb)->ipimap;
	struct iyesmap *imap = JFS_IP(ipimap)->i_imap;
	pxd_t freepxd;
	tid_t tid;
	struct iyesde *iplist[3];
	struct tlock *tlck;
	struct pxd_lock *pxdlock;

	/*
	 * This is just to suppress compiler warnings.  The same logic that
	 * references these variables is used to initialize them.
	 */
	aiagp = biagp = ciagp = diagp = NULL;

	/* get the iag number containing the iyesde.
	 */
	iagyes = INOTOIAG(inum);

	/* make sure that the iag is contained within
	 * the map.
	 */
	if (iagyes >= imap->im_nextiag) {
		print_hex_dump(KERN_ERR, "imap: ", DUMP_PREFIX_ADDRESS, 16, 4,
			       imap, 32, 0);
		jfs_error(ip->i_sb, "inum = %d, iagyes = %d, nextiag = %d\n",
			  (uint) inum, iagyes, imap->im_nextiag);
		return -EIO;
	}

	/* get the allocation group for this iyes.
	 */
	agyes = BLKTOAG(JFS_IP(ip)->agstart, JFS_SBI(ip->i_sb));

	/* Lock the AG specific iyesde map information
	 */
	AG_LOCK(imap, agyes);

	/* Obtain read lock in imap iyesde.  Don't release it until we have
	 * read all of the IAG's that we are going to.
	 */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);

	/* read the iag.
	 */
	if ((rc = diIAGRead(imap, iagyes, &mp))) {
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, agyes);
		return (rc);
	}
	iagp = (struct iag *) mp->data;

	/* get the iyesde number and extent number of the iyesde within
	 * the iag and the iyesde number within the extent.
	 */
	iyes = inum & (INOSPERIAG - 1);
	extyes = iyes >> L2INOSPEREXT;
	bityes = iyes & (INOSPEREXT - 1);
	mask = HIGHORDER >> bityes;

	if (!(le32_to_cpu(iagp->wmap[extyes]) & mask)) {
		jfs_error(ip->i_sb, "wmap shows iyesde already free\n");
	}

	if (!addressPXD(&iagp->iyesext[extyes])) {
		release_metapage(mp);
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, agyes);
		jfs_error(ip->i_sb, "invalid iyesext\n");
		return -EIO;
	}

	/* compute the bitmap for the extent reflecting the freed iyesde.
	 */
	bitmap = le32_to_cpu(iagp->wmap[extyes]) & ~mask;

	if (imap->im_agctl[agyes].numfree > imap->im_agctl[agyes].numiyess) {
		release_metapage(mp);
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, agyes);
		jfs_error(ip->i_sb, "numfree > numiyess\n");
		return -EIO;
	}
	/*
	 *	iyesde extent still has some iyesdes or below low water mark:
	 *	keep the iyesde extent;
	 */
	if (bitmap ||
	    imap->im_agctl[agyes].numfree < 96 ||
	    (imap->im_agctl[agyes].numfree < 288 &&
	     (((imap->im_agctl[agyes].numfree * 100) /
	       imap->im_agctl[agyes].numiyess) <= 25))) {
		/* if the iag currently has yes free iyesdes (i.e.,
		 * the iyesde being freed is the first free iyesde of iag),
		 * insert the iag at head of the iyesde free list for the ag.
		 */
		if (iagp->nfreeiyess == 0) {
			/* check if there are any iags on the ag iyesde
			 * free list.  if so, read the first one so that
			 * we can link the current iag onto the list at
			 * the head.
			 */
			if ((fwd = imap->im_agctl[agyes].iyesfree) >= 0) {
				/* read the iag that currently is the head
				 * of the list.
				 */
				if ((rc = diIAGRead(imap, fwd, &amp))) {
					IREAD_UNLOCK(ipimap);
					AG_UNLOCK(imap, agyes);
					release_metapage(mp);
					return (rc);
				}
				aiagp = (struct iag *) amp->data;

				/* make current head point back to the iag.
				 */
				aiagp->iyesfreeback = cpu_to_le32(iagyes);

				write_metapage(amp);
			}

			/* iag points forward to current head and iag
			 * becomes the new head of the list.
			 */
			iagp->iyesfreefwd =
			    cpu_to_le32(imap->im_agctl[agyes].iyesfree);
			iagp->iyesfreeback = cpu_to_le32(-1);
			imap->im_agctl[agyes].iyesfree = iagyes;
		}
		IREAD_UNLOCK(ipimap);

		/* update the free iyesde summary map for the extent if
		 * freeing the iyesde means the extent will yesw have free
		 * iyesdes (i.e., the iyesde being freed is the first free
		 * iyesde of extent),
		 */
		if (iagp->wmap[extyes] == cpu_to_le32(ONES)) {
			sword = extyes >> L2EXTSPERSUM;
			bityes = extyes & (EXTSPERSUM - 1);
			iagp->iyessmap[sword] &=
			    cpu_to_le32(~(HIGHORDER >> bityes));
		}

		/* update the bitmap.
		 */
		iagp->wmap[extyes] = cpu_to_le32(bitmap);

		/* update the free iyesde counts at the iag, ag and
		 * map level.
		 */
		le32_add_cpu(&iagp->nfreeiyess, 1);
		imap->im_agctl[agyes].numfree += 1;
		atomic_inc(&imap->im_numfree);

		/* release the AG iyesde map lock
		 */
		AG_UNLOCK(imap, agyes);

		/* write the iag */
		write_metapage(mp);

		return (0);
	}


	/*
	 *	iyesde extent has become free and above low water mark:
	 *	free the iyesde extent;
	 */

	/*
	 *	prepare to update iag list(s) (careful update step 1)
	 */
	amp = bmp = cmp = dmp = NULL;
	fwd = back = -1;

	/* check if the iag currently has yes free extents.  if so,
	 * it will be placed on the head of the ag extent free list.
	 */
	if (iagp->nfreeexts == 0) {
		/* check if the ag extent free list has any iags.
		 * if so, read the iag at the head of the list yesw.
		 * this (head) iag will be updated later to reflect
		 * the addition of the current iag at the head of
		 * the list.
		 */
		if ((fwd = imap->im_agctl[agyes].extfree) >= 0) {
			if ((rc = diIAGRead(imap, fwd, &amp)))
				goto error_out;
			aiagp = (struct iag *) amp->data;
		}
	} else {
		/* iag has free extents. check if the addition of a free
		 * extent will cause all extents to be free within this
		 * iag.  if so, the iag will be removed from the ag extent
		 * free list and placed on the iyesde map's free iag list.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG - 1)) {
			/* in preparation for removing the iag from the
			 * ag extent free list, read the iags preceding
			 * and following the iag on the ag extent free
			 * list.
			 */
			if ((fwd = le32_to_cpu(iagp->extfreefwd)) >= 0) {
				if ((rc = diIAGRead(imap, fwd, &amp)))
					goto error_out;
				aiagp = (struct iag *) amp->data;
			}

			if ((back = le32_to_cpu(iagp->extfreeback)) >= 0) {
				if ((rc = diIAGRead(imap, back, &bmp)))
					goto error_out;
				biagp = (struct iag *) bmp->data;
			}
		}
	}

	/* remove the iag from the ag iyesde free list if freeing
	 * this extent cause the iag to have yes free iyesdes.
	 */
	if (iagp->nfreeiyess == cpu_to_le32(INOSPEREXT - 1)) {
		int iyesfreeback = le32_to_cpu(iagp->iyesfreeback);
		int iyesfreefwd = le32_to_cpu(iagp->iyesfreefwd);

		/* in preparation for removing the iag from the
		 * ag iyesde free list, read the iags preceding
		 * and following the iag on the ag iyesde free
		 * list.  before reading these iags, we must make
		 * sure that we already don't have them in hand
		 * from up above, since re-reading an iag (buffer)
		 * we are currently holding would cause a deadlock.
		 */
		if (iyesfreefwd >= 0) {

			if (iyesfreefwd == fwd)
				ciagp = (struct iag *) amp->data;
			else if (iyesfreefwd == back)
				ciagp = (struct iag *) bmp->data;
			else {
				if ((rc =
				     diIAGRead(imap, iyesfreefwd, &cmp)))
					goto error_out;
				ciagp = (struct iag *) cmp->data;
			}
			assert(ciagp != NULL);
		}

		if (iyesfreeback >= 0) {
			if (iyesfreeback == fwd)
				diagp = (struct iag *) amp->data;
			else if (iyesfreeback == back)
				diagp = (struct iag *) bmp->data;
			else {
				if ((rc =
				     diIAGRead(imap, iyesfreeback, &dmp)))
					goto error_out;
				diagp = (struct iag *) dmp->data;
			}
			assert(diagp != NULL);
		}
	}

	IREAD_UNLOCK(ipimap);

	/*
	 * invalidate any page of the iyesde extent freed from buffer cache;
	 */
	freepxd = iagp->iyesext[extyes];
	invalidate_pxd_metapages(ip, freepxd);

	/*
	 *	update iag list(s) (careful update step 2)
	 */
	/* add the iag to the ag extent free list if this is the
	 * first free extent for the iag.
	 */
	if (iagp->nfreeexts == 0) {
		if (fwd >= 0)
			aiagp->extfreeback = cpu_to_le32(iagyes);

		iagp->extfreefwd =
		    cpu_to_le32(imap->im_agctl[agyes].extfree);
		iagp->extfreeback = cpu_to_le32(-1);
		imap->im_agctl[agyes].extfree = iagyes;
	} else {
		/* remove the iag from the ag extent list if all extents
		 * are yesw free and place it on the iyesde map iag free list.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG - 1)) {
			if (fwd >= 0)
				aiagp->extfreeback = iagp->extfreeback;

			if (back >= 0)
				biagp->extfreefwd = iagp->extfreefwd;
			else
				imap->im_agctl[agyes].extfree =
				    le32_to_cpu(iagp->extfreefwd);

			iagp->extfreefwd = iagp->extfreeback = cpu_to_le32(-1);

			IAGFREE_LOCK(imap);
			iagp->iagfree = cpu_to_le32(imap->im_freeiag);
			imap->im_freeiag = iagyes;
			IAGFREE_UNLOCK(imap);
		}
	}

	/* remove the iag from the ag iyesde free list if freeing
	 * this extent causes the iag to have yes free iyesdes.
	 */
	if (iagp->nfreeiyess == cpu_to_le32(INOSPEREXT - 1)) {
		if ((int) le32_to_cpu(iagp->iyesfreefwd) >= 0)
			ciagp->iyesfreeback = iagp->iyesfreeback;

		if ((int) le32_to_cpu(iagp->iyesfreeback) >= 0)
			diagp->iyesfreefwd = iagp->iyesfreefwd;
		else
			imap->im_agctl[agyes].iyesfree =
			    le32_to_cpu(iagp->iyesfreefwd);

		iagp->iyesfreefwd = iagp->iyesfreeback = cpu_to_le32(-1);
	}

	/* update the iyesde extent address and working map
	 * to reflect the free extent.
	 * the permanent map should have been updated already
	 * for the iyesde being freed.
	 */
	if (iagp->pmap[extyes] != 0) {
		jfs_error(ip->i_sb, "the pmap does yest show iyesde free\n");
	}
	iagp->wmap[extyes] = 0;
	PXDlength(&iagp->iyesext[extyes], 0);
	PXDaddress(&iagp->iyesext[extyes], 0);

	/* update the free extent and free iyesde summary maps
	 * to reflect the freed extent.
	 * the iyesde summary map is marked to indicate yes iyesdes
	 * available for the freed extent.
	 */
	sword = extyes >> L2EXTSPERSUM;
	bityes = extyes & (EXTSPERSUM - 1);
	mask = HIGHORDER >> bityes;
	iagp->iyessmap[sword] |= cpu_to_le32(mask);
	iagp->extsmap[sword] &= cpu_to_le32(~mask);

	/* update the number of free iyesdes and number of free extents
	 * for the iag.
	 */
	le32_add_cpu(&iagp->nfreeiyess, -(INOSPEREXT - 1));
	le32_add_cpu(&iagp->nfreeexts, 1);

	/* update the number of free iyesdes and backed iyesdes
	 * at the ag and iyesde map level.
	 */
	imap->im_agctl[agyes].numfree -= (INOSPEREXT - 1);
	imap->im_agctl[agyes].numiyess -= INOSPEREXT;
	atomic_sub(INOSPEREXT - 1, &imap->im_numfree);
	atomic_sub(INOSPEREXT, &imap->im_numiyess);

	if (amp)
		write_metapage(amp);
	if (bmp)
		write_metapage(bmp);
	if (cmp)
		write_metapage(cmp);
	if (dmp)
		write_metapage(dmp);

	/*
	 * start transaction to update block allocation map
	 * for the iyesde extent freed;
	 *
	 * N.B. AG_LOCK is released and iag will be released below, and
	 * other thread may allocate iyesde from/reusing the ixad freed
	 * BUT with new/different backing iyesde extent from the extent
	 * to be freed by the transaction;
	 */
	tid = txBegin(ipimap->i_sb, COMMIT_FORCE);
	mutex_lock(&JFS_IP(ipimap)->commit_mutex);

	/* acquire tlock of the iag page of the freed ixad
	 * to force the page NOHOMEOK (even though yes data is
	 * logged from the iag page) until NOREDOPAGE|FREEXTENT log
	 * for the free of the extent is committed;
	 * write FREEXTENT|NOREDOPAGE log record
	 * N.B. linelock is overlaid as freed extent descriptor;
	 */
	tlck = txLock(tid, ipimap, mp, tlckINODE | tlckFREE);
	pxdlock = (struct pxd_lock *) & tlck->lock;
	pxdlock->flag = mlckFREEPXD;
	pxdlock->pxd = freepxd;
	pxdlock->index = 1;

	write_metapage(mp);

	iplist[0] = ipimap;

	/*
	 * logredo needs the IAG number and IAG extent index in order
	 * to ensure that the IMap is consistent.  The least disruptive
	 * way to pass these values through  to the transaction manager
	 * is in the iplist array.
	 *
	 * It's yest pretty, but it works.
	 */
	iplist[1] = (struct iyesde *) (size_t)iagyes;
	iplist[2] = (struct iyesde *) (size_t)extyes;

	rc = txCommit(tid, 1, &iplist[0], COMMIT_FORCE);

	txEnd(tid);
	mutex_unlock(&JFS_IP(ipimap)->commit_mutex);

	/* unlock the AG iyesde map information */
	AG_UNLOCK(imap, agyes);

	return (0);

      error_out:
	IREAD_UNLOCK(ipimap);

	if (amp)
		release_metapage(amp);
	if (bmp)
		release_metapage(bmp);
	if (cmp)
		release_metapage(cmp);
	if (dmp)
		release_metapage(dmp);

	AG_UNLOCK(imap, agyes);

	release_metapage(mp);

	return (rc);
}

/*
 * There are several places in the diAlloc* routines where we initialize
 * the iyesde.
 */
static inline void
diInitIyesde(struct iyesde *ip, int iagyes, int iyes, int extyes, struct iag * iagp)
{
	struct jfs_iyesde_info *jfs_ip = JFS_IP(ip);

	ip->i_iyes = (iagyes << L2INOSPERIAG) + iyes;
	jfs_ip->ixpxd = iagp->iyesext[extyes];
	jfs_ip->agstart = le64_to_cpu(iagp->agstart);
	jfs_ip->active_ag = -1;
}


/*
 * NAME:	diAlloc(pip,dir,ip)
 *
 * FUNCTION:	allocate a disk iyesde from the iyesde working map
 *		for a fileset or aggregate.
 *
 * PARAMETERS:
 *	pip	- pointer to incore iyesde for the parent iyesde.
 *	dir	- 'true' if the new disk iyesde is for a directory.
 *	ip	- pointer to a new iyesde
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
int diAlloc(struct iyesde *pip, bool dir, struct iyesde *ip)
{
	int rc, iyes, iagyes, addext, extyes, bityes, sword;
	int nwords, rem, i, agyes;
	u32 mask, iyessmap, extsmap;
	struct iyesde *ipimap;
	struct metapage *mp;
	iyes_t inum;
	struct iag *iagp;
	struct iyesmap *imap;

	/* get the pointers to the iyesde map iyesde and the
	 * corresponding imap control structure.
	 */
	ipimap = JFS_SBI(pip->i_sb)->ipimap;
	imap = JFS_IP(ipimap)->i_imap;
	JFS_IP(ip)->ipimap = ipimap;
	JFS_IP(ip)->fileset = FILESYSTEM_I;

	/* for a directory, the allocation policy is to start
	 * at the ag level using the preferred ag.
	 */
	if (dir) {
		agyes = dbNextAG(JFS_SBI(pip->i_sb)->ipbmap);
		AG_LOCK(imap, agyes);
		goto tryag;
	}

	/* for files, the policy starts off by trying to allocate from
	 * the same iag containing the parent disk iyesde:
	 * try to allocate the new disk iyesde close to the parent disk
	 * iyesde, using parent disk iyesde number + 1 as the allocation
	 * hint.  (we use a left-to-right policy to attempt to avoid
	 * moving backward on the disk.)  compute the hint within the
	 * file system and the iag.
	 */

	/* get the ag number of this iag */
	agyes = BLKTOAG(JFS_IP(pip)->agstart, JFS_SBI(pip->i_sb));

	if (atomic_read(&JFS_SBI(pip->i_sb)->bmap->db_active[agyes])) {
		/*
		 * There is an open file actively growing.  We want to
		 * allocate new iyesdes from a different ag to avoid
		 * fragmentation problems.
		 */
		agyes = dbNextAG(JFS_SBI(pip->i_sb)->ipbmap);
		AG_LOCK(imap, agyes);
		goto tryag;
	}

	inum = pip->i_iyes + 1;
	iyes = inum & (INOSPERIAG - 1);

	/* back off the hint if it is outside of the iag */
	if (iyes == 0)
		inum = pip->i_iyes;

	/* lock the AG iyesde map information */
	AG_LOCK(imap, agyes);

	/* Get read lock on imap iyesde */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);

	/* get the iag number and read the iag */
	iagyes = INOTOIAG(inum);
	if ((rc = diIAGRead(imap, iagyes, &mp))) {
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, agyes);
		return (rc);
	}
	iagp = (struct iag *) mp->data;

	/* determine if new iyesde extent is allowed to be added to the iag.
	 * new iyesde extent can be added to the iag if the ag
	 * has less than 32 free disk iyesdes and the iag has free extents.
	 */
	addext = (imap->im_agctl[agyes].numfree < 32 && iagp->nfreeexts);

	/*
	 *	try to allocate from the IAG
	 */
	/* check if the iyesde may be allocated from the iag
	 * (i.e. the iyesde has free iyesdes or new extent can be added).
	 */
	if (iagp->nfreeiyess || addext) {
		/* determine the extent number of the hint.
		 */
		extyes = iyes >> L2INOSPEREXT;

		/* check if the extent containing the hint has backed
		 * iyesdes.  if so, try to allocate within this extent.
		 */
		if (addressPXD(&iagp->iyesext[extyes])) {
			bityes = iyes & (INOSPEREXT - 1);
			if ((bityes =
			     diFindFree(le32_to_cpu(iagp->wmap[extyes]),
					bityes))
			    < INOSPEREXT) {
				iyes = (extyes << L2INOSPEREXT) + bityes;

				/* a free iyesde (bit) was found within this
				 * extent, so allocate it.
				 */
				rc = diAllocBit(imap, iagp, iyes);
				IREAD_UNLOCK(ipimap);
				if (rc) {
					assert(rc == -EIO);
				} else {
					/* set the results of the allocation
					 * and write the iag.
					 */
					diInitIyesde(ip, iagyes, iyes, extyes,
						    iagp);
					mark_metapage_dirty(mp);
				}
				release_metapage(mp);

				/* free the AG lock and return.
				 */
				AG_UNLOCK(imap, agyes);
				return (rc);
			}

			if (!addext)
				extyes =
				    (extyes ==
				     EXTSPERIAG - 1) ? 0 : extyes + 1;
		}

		/*
		 * yes free iyesdes within the extent containing the hint.
		 *
		 * try to allocate from the backed extents following
		 * hint or, if appropriate (i.e. addext is true), allocate
		 * an extent of free iyesdes at or following the extent
		 * containing the hint.
		 *
		 * the free iyesde and free extent summary maps are used
		 * here, so determine the starting summary map position
		 * and the number of words we'll have to examine.  again,
		 * the approach is to allocate following the hint, so we
		 * might have to initially igyesre prior bits of the summary
		 * map that represent extents prior to the extent containing
		 * the hint and later revisit these bits.
		 */
		bityes = extyes & (EXTSPERSUM - 1);
		nwords = (bityes == 0) ? SMAPSZ : SMAPSZ + 1;
		sword = extyes >> L2EXTSPERSUM;

		/* mask any prior bits for the starting words of the
		 * summary map.
		 */
		mask = (bityes == 0) ? 0 : (ONES << (EXTSPERSUM - bityes));
		iyessmap = le32_to_cpu(iagp->iyessmap[sword]) | mask;
		extsmap = le32_to_cpu(iagp->extsmap[sword]) | mask;

		/* scan the free iyesde and free extent summary maps for
		 * free resources.
		 */
		for (i = 0; i < nwords; i++) {
			/* check if this word of the free iyesde summary
			 * map describes an extent with free iyesdes.
			 */
			if (~iyessmap) {
				/* an extent with free iyesdes has been
				 * found. determine the extent number
				 * and the iyesde number within the extent.
				 */
				rem = diFindFree(iyessmap, 0);
				extyes = (sword << L2EXTSPERSUM) + rem;
				rem = diFindFree(le32_to_cpu(iagp->wmap[extyes]),
						 0);
				if (rem >= INOSPEREXT) {
					IREAD_UNLOCK(ipimap);
					release_metapage(mp);
					AG_UNLOCK(imap, agyes);
					jfs_error(ip->i_sb,
						  "can't find free bit in wmap\n");
					return -EIO;
				}

				/* determine the iyesde number within the
				 * iag and allocate the iyesde from the
				 * map.
				 */
				iyes = (extyes << L2INOSPEREXT) + rem;
				rc = diAllocBit(imap, iagp, iyes);
				IREAD_UNLOCK(ipimap);
				if (rc)
					assert(rc == -EIO);
				else {
					/* set the results of the allocation
					 * and write the iag.
					 */
					diInitIyesde(ip, iagyes, iyes, extyes,
						    iagp);
					mark_metapage_dirty(mp);
				}
				release_metapage(mp);

				/* free the AG lock and return.
				 */
				AG_UNLOCK(imap, agyes);
				return (rc);

			}

			/* check if we may allocate an extent of free
			 * iyesdes and whether this word of the free
			 * extents summary map describes a free extent.
			 */
			if (addext && ~extsmap) {
				/* a free extent has been found.  determine
				 * the extent number.
				 */
				rem = diFindFree(extsmap, 0);
				extyes = (sword << L2EXTSPERSUM) + rem;

				/* allocate an extent of free iyesdes.
				 */
				if ((rc = diNewExt(imap, iagp, extyes))) {
					/* if there is yes disk space for a
					 * new extent, try to allocate the
					 * disk iyesde from somewhere else.
					 */
					if (rc == -ENOSPC)
						break;

					assert(rc == -EIO);
				} else {
					/* set the results of the allocation
					 * and write the iag.
					 */
					diInitIyesde(ip, iagyes,
						    extyes << L2INOSPEREXT,
						    extyes, iagp);
					mark_metapage_dirty(mp);
				}
				release_metapage(mp);
				/* free the imap iyesde & the AG lock & return.
				 */
				IREAD_UNLOCK(ipimap);
				AG_UNLOCK(imap, agyes);
				return (rc);
			}

			/* move on to the next set of summary map words.
			 */
			sword = (sword == SMAPSZ - 1) ? 0 : sword + 1;
			iyessmap = le32_to_cpu(iagp->iyessmap[sword]);
			extsmap = le32_to_cpu(iagp->extsmap[sword]);
		}
	}
	/* unlock imap iyesde */
	IREAD_UNLOCK(ipimap);

	/* yesthing doing in this iag, so release it. */
	release_metapage(mp);

      tryag:
	/*
	 * try to allocate anywhere within the same AG as the parent iyesde.
	 */
	rc = diAllocAG(imap, agyes, dir, ip);

	AG_UNLOCK(imap, agyes);

	if (rc != -ENOSPC)
		return (rc);

	/*
	 * try to allocate in any AG.
	 */
	return (diAllocAny(imap, agyes, dir, ip));
}


/*
 * NAME:	diAllocAG(imap,agyes,dir,ip)
 *
 * FUNCTION:	allocate a disk iyesde from the allocation group.
 *
 *		this routine first determines if a new extent of free
 *		iyesdes should be added for the allocation group, with
 *		the current request satisfied from this extent. if this
 *		is the case, an attempt will be made to do just that.  if
 *		this attempt fails or it has been determined that a new
 *		extent should yest be added, an attempt is made to satisfy
 *		the request by allocating an existing (backed) free iyesde
 *		from the allocation group.
 *
 * PRE CONDITION: Already have the AG lock for this AG.
 *
 * PARAMETERS:
 *	imap	- pointer to iyesde map control structure.
 *	agyes	- allocation group to allocate from.
 *	dir	- 'true' if the new disk iyesde is for a directory.
 *	ip	- pointer to the new iyesde to be filled in on successful return
 *		  with the disk iyesde number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int
diAllocAG(struct iyesmap * imap, int agyes, bool dir, struct iyesde *ip)
{
	int rc, addext, numfree, numiyess;

	/* get the number of free and the number of backed disk
	 * iyesdes currently within the ag.
	 */
	numfree = imap->im_agctl[agyes].numfree;
	numiyess = imap->im_agctl[agyes].numiyess;

	if (numfree > numiyess) {
		jfs_error(ip->i_sb, "numfree > numiyess\n");
		return -EIO;
	}

	/* determine if we should allocate a new extent of free iyesdes
	 * within the ag: for directory iyesdes, add a new extent
	 * if there are a small number of free iyesdes or number of free
	 * iyesdes is a small percentage of the number of backed iyesdes.
	 */
	if (dir)
		addext = (numfree < 64 ||
			  (numfree < 256
			   && ((numfree * 100) / numiyess) <= 20));
	else
		addext = (numfree == 0);

	/*
	 * try to allocate a new extent of free iyesdes.
	 */
	if (addext) {
		/* if free space is yest available for this new extent, try
		 * below to allocate a free and existing (already backed)
		 * iyesde from the ag.
		 */
		if ((rc = diAllocExt(imap, agyes, ip)) != -ENOSPC)
			return (rc);
	}

	/*
	 * try to allocate an existing free iyesde from the ag.
	 */
	return (diAllocIyes(imap, agyes, ip));
}


/*
 * NAME:	diAllocAny(imap,agyes,dir,iap)
 *
 * FUNCTION:	allocate a disk iyesde from any other allocation group.
 *
 *		this routine is called when an allocation attempt within
 *		the primary allocation group has failed. if attempts to
 *		allocate an iyesde from any allocation group other than the
 *		specified primary group.
 *
 * PARAMETERS:
 *	imap	- pointer to iyesde map control structure.
 *	agyes	- primary allocation group (to avoid).
 *	dir	- 'true' if the new disk iyesde is for a directory.
 *	ip	- pointer to a new iyesde to be filled in on successful return
 *		  with the disk iyesde number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int
diAllocAny(struct iyesmap * imap, int agyes, bool dir, struct iyesde *ip)
{
	int ag, rc;
	int maxag = JFS_SBI(imap->im_ipimap->i_sb)->bmap->db_maxag;


	/* try to allocate from the ags following agyes up to
	 * the maximum ag number.
	 */
	for (ag = agyes + 1; ag <= maxag; ag++) {
		AG_LOCK(imap, ag);

		rc = diAllocAG(imap, ag, dir, ip);

		AG_UNLOCK(imap, ag);

		if (rc != -ENOSPC)
			return (rc);
	}

	/* try to allocate from the ags in front of agyes.
	 */
	for (ag = 0; ag < agyes; ag++) {
		AG_LOCK(imap, ag);

		rc = diAllocAG(imap, ag, dir, ip);

		AG_UNLOCK(imap, ag);

		if (rc != -ENOSPC)
			return (rc);
	}

	/* yes free disk iyesdes.
	 */
	return -ENOSPC;
}


/*
 * NAME:	diAllocIyes(imap,agyes,ip)
 *
 * FUNCTION:	allocate a disk iyesde from the allocation group's free
 *		iyesde list, returning an error if this free list is
 *		empty (i.e. yes iags on the list).
 *
 *		allocation occurs from the first iag on the list using
 *		the iag's free iyesde summary map to find the leftmost
 *		free iyesde in the iag.
 *
 * PRE CONDITION: Already have AG lock for this AG.
 *
 * PARAMETERS:
 *	imap	- pointer to iyesde map control structure.
 *	agyes	- allocation group.
 *	ip	- pointer to new iyesde to be filled in on successful return
 *		  with the disk iyesde number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diAllocIyes(struct iyesmap * imap, int agyes, struct iyesde *ip)
{
	int iagyes, iyes, rc, rem, extyes, sword;
	struct metapage *mp;
	struct iag *iagp;

	/* check if there are iags on the ag's free iyesde list.
	 */
	if ((iagyes = imap->im_agctl[agyes].iyesfree) < 0)
		return -ENOSPC;

	/* obtain read lock on imap iyesde */
	IREAD_LOCK(imap->im_ipimap, RDWRLOCK_IMAP);

	/* read the iag at the head of the list.
	 */
	if ((rc = diIAGRead(imap, iagyes, &mp))) {
		IREAD_UNLOCK(imap->im_ipimap);
		return (rc);
	}
	iagp = (struct iag *) mp->data;

	/* better be free iyesdes in this iag if it is on the
	 * list.
	 */
	if (!iagp->nfreeiyess) {
		IREAD_UNLOCK(imap->im_ipimap);
		release_metapage(mp);
		jfs_error(ip->i_sb, "nfreeiyess = 0, but iag on freelist\n");
		return -EIO;
	}

	/* scan the free iyesde summary map to find an extent
	 * with free iyesdes.
	 */
	for (sword = 0;; sword++) {
		if (sword >= SMAPSZ) {
			IREAD_UNLOCK(imap->im_ipimap);
			release_metapage(mp);
			jfs_error(ip->i_sb,
				  "free iyesde yest found in summary map\n");
			return -EIO;
		}

		if (~iagp->iyessmap[sword])
			break;
	}

	/* found a extent with free iyesdes. determine
	 * the extent number.
	 */
	rem = diFindFree(le32_to_cpu(iagp->iyessmap[sword]), 0);
	if (rem >= EXTSPERSUM) {
		IREAD_UNLOCK(imap->im_ipimap);
		release_metapage(mp);
		jfs_error(ip->i_sb, "yes free extent found\n");
		return -EIO;
	}
	extyes = (sword << L2EXTSPERSUM) + rem;

	/* find the first free iyesde in the extent.
	 */
	rem = diFindFree(le32_to_cpu(iagp->wmap[extyes]), 0);
	if (rem >= INOSPEREXT) {
		IREAD_UNLOCK(imap->im_ipimap);
		release_metapage(mp);
		jfs_error(ip->i_sb, "free iyesde yest found\n");
		return -EIO;
	}

	/* compute the iyesde number within the iag.
	 */
	iyes = (extyes << L2INOSPEREXT) + rem;

	/* allocate the iyesde.
	 */
	rc = diAllocBit(imap, iagp, iyes);
	IREAD_UNLOCK(imap->im_ipimap);
	if (rc) {
		release_metapage(mp);
		return (rc);
	}

	/* set the results of the allocation and write the iag.
	 */
	diInitIyesde(ip, iagyes, iyes, extyes, iagp);
	write_metapage(mp);

	return (0);
}


/*
 * NAME:	diAllocExt(imap,agyes,ip)
 *
 * FUNCTION:	add a new extent of free iyesdes to an iag, allocating
 *		an iyesde from this extent to satisfy the current allocation
 *		request.
 *
 *		this routine first tries to find an existing iag with free
 *		extents through the ag free extent list.  if list is yest
 *		empty, the head of the list will be selected as the home
 *		of the new extent of free iyesdes.  otherwise (the list is
 *		empty), a new iag will be allocated for the ag to contain
 *		the extent.
 *
 *		once an iag has been selected, the free extent summary map
 *		is used to locate a free extent within the iag and diNewExt()
 *		is called to initialize the extent, with initialization
 *		including the allocation of the first iyesde of the extent
 *		for the purpose of satisfying this request.
 *
 * PARAMETERS:
 *	imap	- pointer to iyesde map control structure.
 *	agyes	- allocation group number.
 *	ip	- pointer to new iyesde to be filled in on successful return
 *		  with the disk iyesde number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diAllocExt(struct iyesmap * imap, int agyes, struct iyesde *ip)
{
	int rem, iagyes, sword, extyes, rc;
	struct metapage *mp;
	struct iag *iagp;

	/* check if the ag has any iags with free extents.  if yest,
	 * allocate a new iag for the ag.
	 */
	if ((iagyes = imap->im_agctl[agyes].extfree) < 0) {
		/* If successful, diNewIAG will obtain the read lock on the
		 * imap iyesde.
		 */
		if ((rc = diNewIAG(imap, &iagyes, agyes, &mp))) {
			return (rc);
		}
		iagp = (struct iag *) mp->data;

		/* set the ag number if this a brand new iag
		 */
		iagp->agstart =
		    cpu_to_le64(AGTOBLK(agyes, imap->im_ipimap));
	} else {
		/* read the iag.
		 */
		IREAD_LOCK(imap->im_ipimap, RDWRLOCK_IMAP);
		if ((rc = diIAGRead(imap, iagyes, &mp))) {
			IREAD_UNLOCK(imap->im_ipimap);
			jfs_error(ip->i_sb, "error reading iag\n");
			return rc;
		}
		iagp = (struct iag *) mp->data;
	}

	/* using the free extent summary map, find a free extent.
	 */
	for (sword = 0;; sword++) {
		if (sword >= SMAPSZ) {
			release_metapage(mp);
			IREAD_UNLOCK(imap->im_ipimap);
			jfs_error(ip->i_sb, "free ext summary map yest found\n");
			return -EIO;
		}
		if (~iagp->extsmap[sword])
			break;
	}

	/* determine the extent number of the free extent.
	 */
	rem = diFindFree(le32_to_cpu(iagp->extsmap[sword]), 0);
	if (rem >= EXTSPERSUM) {
		release_metapage(mp);
		IREAD_UNLOCK(imap->im_ipimap);
		jfs_error(ip->i_sb, "free extent yest found\n");
		return -EIO;
	}
	extyes = (sword << L2EXTSPERSUM) + rem;

	/* initialize the new extent.
	 */
	rc = diNewExt(imap, iagp, extyes);
	IREAD_UNLOCK(imap->im_ipimap);
	if (rc) {
		/* something bad happened.  if a new iag was allocated,
		 * place it back on the iyesde map's iag free list, and
		 * clear the ag number information.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG)) {
			IAGFREE_LOCK(imap);
			iagp->iagfree = cpu_to_le32(imap->im_freeiag);
			imap->im_freeiag = iagyes;
			IAGFREE_UNLOCK(imap);
		}
		write_metapage(mp);
		return (rc);
	}

	/* set the results of the allocation and write the iag.
	 */
	diInitIyesde(ip, iagyes, extyes << L2INOSPEREXT, extyes, iagp);

	write_metapage(mp);

	return (0);
}


/*
 * NAME:	diAllocBit(imap,iagp,iyes)
 *
 * FUNCTION:	allocate a backed iyesde from an iag.
 *
 *		this routine performs the mechanics of allocating a
 *		specified iyesde from a backed extent.
 *
 *		if the iyesde to be allocated represents the last free
 *		iyesde within the iag, the iag will be removed from the
 *		ag free iyesde list.
 *
 *		a careful update approach is used to provide consistency
 *		in the face of updates to multiple buffers.  under this
 *		approach, all required buffers are obtained before making
 *		any updates and are held all are updates are complete.
 *
 * PRE CONDITION: Already have buffer lock on iagp.  Already have AG lock on
 *	this AG.  Must have read lock on imap iyesde.
 *
 * PARAMETERS:
 *	imap	- pointer to iyesde map control structure.
 *	iagp	- pointer to iag.
 *	iyes	- iyesde number to be allocated within the iag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diAllocBit(struct iyesmap * imap, struct iag * iagp, int iyes)
{
	int extyes, bityes, agyes, sword, rc;
	struct metapage *amp = NULL, *bmp = NULL;
	struct iag *aiagp = NULL, *biagp = NULL;
	u32 mask;

	/* check if this is the last free iyesde within the iag.
	 * if so, it will have to be removed from the ag free
	 * iyesde list, so get the iags preceding and following
	 * it on the list.
	 */
	if (iagp->nfreeiyess == cpu_to_le32(1)) {
		if ((int) le32_to_cpu(iagp->iyesfreefwd) >= 0) {
			if ((rc =
			     diIAGRead(imap, le32_to_cpu(iagp->iyesfreefwd),
				       &amp)))
				return (rc);
			aiagp = (struct iag *) amp->data;
		}

		if ((int) le32_to_cpu(iagp->iyesfreeback) >= 0) {
			if ((rc =
			     diIAGRead(imap,
				       le32_to_cpu(iagp->iyesfreeback),
				       &bmp))) {
				if (amp)
					release_metapage(amp);
				return (rc);
			}
			biagp = (struct iag *) bmp->data;
		}
	}

	/* get the ag number, extent number, iyesde number within
	 * the extent.
	 */
	agyes = BLKTOAG(le64_to_cpu(iagp->agstart), JFS_SBI(imap->im_ipimap->i_sb));
	extyes = iyes >> L2INOSPEREXT;
	bityes = iyes & (INOSPEREXT - 1);

	/* compute the mask for setting the map.
	 */
	mask = HIGHORDER >> bityes;

	/* the iyesde should be free and backed.
	 */
	if (((le32_to_cpu(iagp->pmap[extyes]) & mask) != 0) ||
	    ((le32_to_cpu(iagp->wmap[extyes]) & mask) != 0) ||
	    (addressPXD(&iagp->iyesext[extyes]) == 0)) {
		if (amp)
			release_metapage(amp);
		if (bmp)
			release_metapage(bmp);

		jfs_error(imap->im_ipimap->i_sb, "iag inconsistent\n");
		return -EIO;
	}

	/* mark the iyesde as allocated in the working map.
	 */
	iagp->wmap[extyes] |= cpu_to_le32(mask);

	/* check if all iyesdes within the extent are yesw
	 * allocated.  if so, update the free iyesde summary
	 * map to reflect this.
	 */
	if (iagp->wmap[extyes] == cpu_to_le32(ONES)) {
		sword = extyes >> L2EXTSPERSUM;
		bityes = extyes & (EXTSPERSUM - 1);
		iagp->iyessmap[sword] |= cpu_to_le32(HIGHORDER >> bityes);
	}

	/* if this was the last free iyesde in the iag, remove the
	 * iag from the ag free iyesde list.
	 */
	if (iagp->nfreeiyess == cpu_to_le32(1)) {
		if (amp) {
			aiagp->iyesfreeback = iagp->iyesfreeback;
			write_metapage(amp);
		}

		if (bmp) {
			biagp->iyesfreefwd = iagp->iyesfreefwd;
			write_metapage(bmp);
		} else {
			imap->im_agctl[agyes].iyesfree =
			    le32_to_cpu(iagp->iyesfreefwd);
		}
		iagp->iyesfreefwd = iagp->iyesfreeback = cpu_to_le32(-1);
	}

	/* update the free iyesde count at the iag, ag, iyesde
	 * map levels.
	 */
	le32_add_cpu(&iagp->nfreeiyess, -1);
	imap->im_agctl[agyes].numfree -= 1;
	atomic_dec(&imap->im_numfree);

	return (0);
}


/*
 * NAME:	diNewExt(imap,iagp,extyes)
 *
 * FUNCTION:	initialize a new extent of iyesdes for an iag, allocating
 *		the first iyesde of the extent for use for the current
 *		allocation request.
 *
 *		disk resources are allocated for the new extent of iyesdes
 *		and the iyesdes themselves are initialized to reflect their
 *		existence within the extent (i.e. their iyesde numbers and
 *		iyesde extent addresses are set) and their initial state
 *		(mode and link count are set to zero).
 *
 *		if the iag is new, it is yest yet on an ag extent free list
 *		but will yesw be placed on this list.
 *
 *		if the allocation of the new extent causes the iag to
 *		have yes free extent, the iag will be removed from the
 *		ag extent free list.
 *
 *		if the iag has yes free backed iyesdes, it will be placed
 *		on the ag free iyesde list, since the addition of the new
 *		extent will yesw cause it to have free iyesdes.
 *
 *		a careful update approach is used to provide consistency
 *		(i.e. list consistency) in the face of updates to multiple
 *		buffers.  under this approach, all required buffers are
 *		obtained before making any updates and are held until all
 *		updates are complete.
 *
 * PRE CONDITION: Already have buffer lock on iagp.  Already have AG lock on
 *	this AG.  Must have read lock on imap iyesde.
 *
 * PARAMETERS:
 *	imap	- pointer to iyesde map control structure.
 *	iagp	- pointer to iag.
 *	extyes	- extent number.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diNewExt(struct iyesmap * imap, struct iag * iagp, int extyes)
{
	int agyes, iagyes, fwd, back, freei = 0, sword, rc;
	struct iag *aiagp = NULL, *biagp = NULL, *ciagp = NULL;
	struct metapage *amp, *bmp, *cmp, *dmp;
	struct iyesde *ipimap;
	s64 blkyes, hint;
	int i, j;
	u32 mask;
	iyes_t iyes;
	struct diyesde *dp;
	struct jfs_sb_info *sbi;

	/* better have free extents.
	 */
	if (!iagp->nfreeexts) {
		jfs_error(imap->im_ipimap->i_sb, "yes free extents\n");
		return -EIO;
	}

	/* get the iyesde map iyesde.
	 */
	ipimap = imap->im_ipimap;
	sbi = JFS_SBI(ipimap->i_sb);

	amp = bmp = cmp = NULL;

	/* get the ag and iag numbers for this iag.
	 */
	agyes = BLKTOAG(le64_to_cpu(iagp->agstart), sbi);
	iagyes = le32_to_cpu(iagp->iagnum);

	/* check if this is the last free extent within the
	 * iag.  if so, the iag must be removed from the ag
	 * free extent list, so get the iags preceding and
	 * following the iag on this list.
	 */
	if (iagp->nfreeexts == cpu_to_le32(1)) {
		if ((fwd = le32_to_cpu(iagp->extfreefwd)) >= 0) {
			if ((rc = diIAGRead(imap, fwd, &amp)))
				return (rc);
			aiagp = (struct iag *) amp->data;
		}

		if ((back = le32_to_cpu(iagp->extfreeback)) >= 0) {
			if ((rc = diIAGRead(imap, back, &bmp)))
				goto error_out;
			biagp = (struct iag *) bmp->data;
		}
	} else {
		/* the iag has free extents.  if all extents are free
		 * (as is the case for a newly allocated iag), the iag
		 * must be added to the ag free extent list, so get
		 * the iag at the head of the list in preparation for
		 * adding this iag to this list.
		 */
		fwd = back = -1;
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG)) {
			if ((fwd = imap->im_agctl[agyes].extfree) >= 0) {
				if ((rc = diIAGRead(imap, fwd, &amp)))
					goto error_out;
				aiagp = (struct iag *) amp->data;
			}
		}
	}

	/* check if the iag has yes free iyesdes.  if so, the iag
	 * will have to be added to the ag free iyesde list, so get
	 * the iag at the head of the list in preparation for
	 * adding this iag to this list.  in doing this, we must
	 * check if we already have the iag at the head of
	 * the list in hand.
	 */
	if (iagp->nfreeiyess == 0) {
		freei = imap->im_agctl[agyes].iyesfree;

		if (freei >= 0) {
			if (freei == fwd) {
				ciagp = aiagp;
			} else if (freei == back) {
				ciagp = biagp;
			} else {
				if ((rc = diIAGRead(imap, freei, &cmp)))
					goto error_out;
				ciagp = (struct iag *) cmp->data;
			}
			if (ciagp == NULL) {
				jfs_error(imap->im_ipimap->i_sb,
					  "ciagp == NULL\n");
				rc = -EIO;
				goto error_out;
			}
		}
	}

	/* allocate disk space for the iyesde extent.
	 */
	if ((extyes == 0) || (addressPXD(&iagp->iyesext[extyes - 1]) == 0))
		hint = ((s64) agyes << sbi->bmap->db_agl2size) - 1;
	else
		hint = addressPXD(&iagp->iyesext[extyes - 1]) +
		    lengthPXD(&iagp->iyesext[extyes - 1]) - 1;

	if ((rc = dbAlloc(ipimap, hint, (s64) imap->im_nbperiext, &blkyes)))
		goto error_out;

	/* compute the iyesde number of the first iyesde within the
	 * extent.
	 */
	iyes = (iagyes << L2INOSPERIAG) + (extyes << L2INOSPEREXT);

	/* initialize the iyesdes within the newly allocated extent a
	 * page at a time.
	 */
	for (i = 0; i < imap->im_nbperiext; i += sbi->nbperpage) {
		/* get a buffer for this page of disk iyesdes.
		 */
		dmp = get_metapage(ipimap, blkyes + i, PSIZE, 1);
		if (dmp == NULL) {
			rc = -EIO;
			goto error_out;
		}
		dp = (struct diyesde *) dmp->data;

		/* initialize the iyesde number, mode, link count and
		 * iyesde extent address.
		 */
		for (j = 0; j < INOSPERPAGE; j++, dp++, iyes++) {
			dp->di_iyesstamp = cpu_to_le32(sbi->iyesstamp);
			dp->di_number = cpu_to_le32(iyes);
			dp->di_fileset = cpu_to_le32(FILESYSTEM_I);
			dp->di_mode = 0;
			dp->di_nlink = 0;
			PXDaddress(&(dp->di_ixpxd), blkyes);
			PXDlength(&(dp->di_ixpxd), imap->im_nbperiext);
		}
		write_metapage(dmp);
	}

	/* if this is the last free extent within the iag, remove the
	 * iag from the ag free extent list.
	 */
	if (iagp->nfreeexts == cpu_to_le32(1)) {
		if (fwd >= 0)
			aiagp->extfreeback = iagp->extfreeback;

		if (back >= 0)
			biagp->extfreefwd = iagp->extfreefwd;
		else
			imap->im_agctl[agyes].extfree =
			    le32_to_cpu(iagp->extfreefwd);

		iagp->extfreefwd = iagp->extfreeback = cpu_to_le32(-1);
	} else {
		/* if the iag has all free extents (newly allocated iag),
		 * add the iag to the ag free extent list.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG)) {
			if (fwd >= 0)
				aiagp->extfreeback = cpu_to_le32(iagyes);

			iagp->extfreefwd = cpu_to_le32(fwd);
			iagp->extfreeback = cpu_to_le32(-1);
			imap->im_agctl[agyes].extfree = iagyes;
		}
	}

	/* if the iag has yes free iyesdes, add the iag to the
	 * ag free iyesde list.
	 */
	if (iagp->nfreeiyess == 0) {
		if (freei >= 0)
			ciagp->iyesfreeback = cpu_to_le32(iagyes);

		iagp->iyesfreefwd =
		    cpu_to_le32(imap->im_agctl[agyes].iyesfree);
		iagp->iyesfreeback = cpu_to_le32(-1);
		imap->im_agctl[agyes].iyesfree = iagyes;
	}

	/* initialize the extent descriptor of the extent. */
	PXDlength(&iagp->iyesext[extyes], imap->im_nbperiext);
	PXDaddress(&iagp->iyesext[extyes], blkyes);

	/* initialize the working and persistent map of the extent.
	 * the working map will be initialized such that
	 * it indicates the first iyesde of the extent is allocated.
	 */
	iagp->wmap[extyes] = cpu_to_le32(HIGHORDER);
	iagp->pmap[extyes] = 0;

	/* update the free iyesde and free extent summary maps
	 * for the extent to indicate the extent has free iyesdes
	 * and yes longer represents a free extent.
	 */
	sword = extyes >> L2EXTSPERSUM;
	mask = HIGHORDER >> (extyes & (EXTSPERSUM - 1));
	iagp->extsmap[sword] |= cpu_to_le32(mask);
	iagp->iyessmap[sword] &= cpu_to_le32(~mask);

	/* update the free iyesde and free extent counts for the
	 * iag.
	 */
	le32_add_cpu(&iagp->nfreeiyess, (INOSPEREXT - 1));
	le32_add_cpu(&iagp->nfreeexts, -1);

	/* update the free and backed iyesde counts for the ag.
	 */
	imap->im_agctl[agyes].numfree += (INOSPEREXT - 1);
	imap->im_agctl[agyes].numiyess += INOSPEREXT;

	/* update the free and backed iyesde counts for the iyesde map.
	 */
	atomic_add(INOSPEREXT - 1, &imap->im_numfree);
	atomic_add(INOSPEREXT, &imap->im_numiyess);

	/* write the iags.
	 */
	if (amp)
		write_metapage(amp);
	if (bmp)
		write_metapage(bmp);
	if (cmp)
		write_metapage(cmp);

	return (0);

      error_out:

	/* release the iags.
	 */
	if (amp)
		release_metapage(amp);
	if (bmp)
		release_metapage(bmp);
	if (cmp)
		release_metapage(cmp);

	return (rc);
}


/*
 * NAME:	diNewIAG(imap,iagyesp,agyes)
 *
 * FUNCTION:	allocate a new iag for an allocation group.
 *
 *		first tries to allocate the iag from the iyesde map
 *		iagfree list:
 *		if the list has free iags, the head of the list is removed
 *		and returned to satisfy the request.
 *		if the iyesde map's iag free list is empty, the iyesde map
 *		is extended to hold a new iag. this new iag is initialized
 *		and returned to satisfy the request.
 *
 * PARAMETERS:
 *	imap	- pointer to iyesde map control structure.
 *	iagyesp	- pointer to an iag number set with the number of the
 *		  newly allocated iag upon successful return.
 *	agyes	- allocation group number.
 *	bpp	- Buffer pointer to be filled in with new IAG's buffer
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 *
 * serialization:
 *	AG lock held on entry/exit;
 *	write lock on the map is held inside;
 *	read lock on the map is held on successful completion;
 *
 * yeste: new iag transaction:
 * . synchroyesusly write iag;
 * . write log of xtree and iyesde of imap;
 * . commit;
 * . synchroyesus write of xtree (right to left, bottom to top);
 * . at start of logredo(): init in-memory imap with one additional iag page;
 * . at end of logredo(): re-read imap iyesde to determine
 *   new imap size;
 */
static int
diNewIAG(struct iyesmap * imap, int *iagyesp, int agyes, struct metapage ** mpp)
{
	int rc;
	int iagyes, i, xlen;
	struct iyesde *ipimap;
	struct super_block *sb;
	struct jfs_sb_info *sbi;
	struct metapage *mp;
	struct iag *iagp;
	s64 xaddr = 0;
	s64 blkyes;
	tid_t tid;
	struct iyesde *iplist[1];

	/* pick up pointers to the iyesde map and mount iyesdes */
	ipimap = imap->im_ipimap;
	sb = ipimap->i_sb;
	sbi = JFS_SBI(sb);

	/* acquire the free iag lock */
	IAGFREE_LOCK(imap);

	/* if there are any iags on the iyesde map free iag list,
	 * allocate the iag from the head of the list.
	 */
	if (imap->im_freeiag >= 0) {
		/* pick up the iag number at the head of the list */
		iagyes = imap->im_freeiag;

		/* determine the logical block number of the iag */
		blkyes = IAGTOLBLK(iagyes, sbi->l2nbperpage);
	} else {
		/* yes free iags. the iyesde map will have to be extented
		 * to include a new iag.
		 */

		/* acquire iyesde map lock */
		IWRITE_LOCK(ipimap, RDWRLOCK_IMAP);

		if (ipimap->i_size >> L2PSIZE != imap->im_nextiag + 1) {
			IWRITE_UNLOCK(ipimap);
			IAGFREE_UNLOCK(imap);
			jfs_error(imap->im_ipimap->i_sb,
				  "ipimap->i_size is wrong\n");
			return -EIO;
		}


		/* get the next available iag number */
		iagyes = imap->im_nextiag;

		/* make sure that we have yest exceeded the maximum iyesde
		 * number limit.
		 */
		if (iagyes > (MAXIAGS - 1)) {
			/* release the iyesde map lock */
			IWRITE_UNLOCK(ipimap);

			rc = -ENOSPC;
			goto out;
		}

		/*
		 * synchroyesusly append new iag page.
		 */
		/* determine the logical address of iag page to append */
		blkyes = IAGTOLBLK(iagyes, sbi->l2nbperpage);

		/* Allocate extent for new iag page */
		xlen = sbi->nbperpage;
		if ((rc = dbAlloc(ipimap, 0, (s64) xlen, &xaddr))) {
			/* release the iyesde map lock */
			IWRITE_UNLOCK(ipimap);

			goto out;
		}

		/*
		 * start transaction of update of the iyesde map
		 * addressing structure pointing to the new iag page;
		 */
		tid = txBegin(sb, COMMIT_FORCE);
		mutex_lock(&JFS_IP(ipimap)->commit_mutex);

		/* update the iyesde map addressing structure to point to it */
		if ((rc =
		     xtInsert(tid, ipimap, 0, blkyes, xlen, &xaddr, 0))) {
			txEnd(tid);
			mutex_unlock(&JFS_IP(ipimap)->commit_mutex);
			/* Free the blocks allocated for the iag since it was
			 * yest successfully added to the iyesde map
			 */
			dbFree(ipimap, xaddr, (s64) xlen);

			/* release the iyesde map lock */
			IWRITE_UNLOCK(ipimap);

			goto out;
		}

		/* update the iyesde map's iyesde to reflect the extension */
		ipimap->i_size += PSIZE;
		iyesde_add_bytes(ipimap, PSIZE);

		/* assign a buffer for the page */
		mp = get_metapage(ipimap, blkyes, PSIZE, 0);
		if (!mp) {
			/*
			 * This is very unlikely since we just created the
			 * extent, but let's try to handle it correctly
			 */
			xtTruncate(tid, ipimap, ipimap->i_size - PSIZE,
				   COMMIT_PWMAP);

			txAbort(tid, 0);
			txEnd(tid);
			mutex_unlock(&JFS_IP(ipimap)->commit_mutex);

			/* release the iyesde map lock */
			IWRITE_UNLOCK(ipimap);

			rc = -EIO;
			goto out;
		}
		iagp = (struct iag *) mp->data;

		/* init the iag */
		memset(iagp, 0, sizeof(struct iag));
		iagp->iagnum = cpu_to_le32(iagyes);
		iagp->iyesfreefwd = iagp->iyesfreeback = cpu_to_le32(-1);
		iagp->extfreefwd = iagp->extfreeback = cpu_to_le32(-1);
		iagp->iagfree = cpu_to_le32(-1);
		iagp->nfreeiyess = 0;
		iagp->nfreeexts = cpu_to_le32(EXTSPERIAG);

		/* initialize the free iyesde summary map (free extent
		 * summary map initialization handled by bzero).
		 */
		for (i = 0; i < SMAPSZ; i++)
			iagp->iyessmap[i] = cpu_to_le32(ONES);

		/*
		 * Write and sync the metapage
		 */
		flush_metapage(mp);

		/*
		 * txCommit(COMMIT_FORCE) will synchroyesusly write address
		 * index pages and iyesde after commit in careful update order
		 * of address index pages (right to left, bottom up);
		 */
		iplist[0] = ipimap;
		rc = txCommit(tid, 1, &iplist[0], COMMIT_FORCE);

		txEnd(tid);
		mutex_unlock(&JFS_IP(ipimap)->commit_mutex);

		duplicateIXtree(sb, blkyes, xlen, &xaddr);

		/* update the next available iag number */
		imap->im_nextiag += 1;

		/* Add the iag to the iag free list so we don't lose the iag
		 * if a failure happens yesw.
		 */
		imap->im_freeiag = iagyes;

		/* Until we have logredo working, we want the imap iyesde &
		 * control page to be up to date.
		 */
		diSync(ipimap);

		/* release the iyesde map lock */
		IWRITE_UNLOCK(ipimap);
	}

	/* obtain read lock on map */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);

	/* read the iag */
	if ((rc = diIAGRead(imap, iagyes, &mp))) {
		IREAD_UNLOCK(ipimap);
		rc = -EIO;
		goto out;
	}
	iagp = (struct iag *) mp->data;

	/* remove the iag from the iag free list */
	imap->im_freeiag = le32_to_cpu(iagp->iagfree);
	iagp->iagfree = cpu_to_le32(-1);

	/* set the return iag number and buffer pointer */
	*iagyesp = iagyes;
	*mpp = mp;

      out:
	/* release the iag free lock */
	IAGFREE_UNLOCK(imap);

	return (rc);
}

/*
 * NAME:	diIAGRead()
 *
 * FUNCTION:	get the buffer for the specified iag within a fileset
 *		or aggregate iyesde map.
 *
 * PARAMETERS:
 *	imap	- pointer to iyesde map control structure.
 *	iagyes	- iag number.
 *	bpp	- point to buffer pointer to be filled in on successful
 *		  exit.
 *
 * SERIALIZATION:
 *	must have read lock on imap iyesde
 *	(When called by diExtendFS, the filesystem is quiesced, therefore
 *	 the read lock is unnecessary.)
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EIO	- i/o error.
 */
static int diIAGRead(struct iyesmap * imap, int iagyes, struct metapage ** mpp)
{
	struct iyesde *ipimap = imap->im_ipimap;
	s64 blkyes;

	/* compute the logical block number of the iag. */
	blkyes = IAGTOLBLK(iagyes, JFS_SBI(ipimap->i_sb)->l2nbperpage);

	/* read the iag. */
	*mpp = read_metapage(ipimap, blkyes, PSIZE, 0);
	if (*mpp == NULL) {
		return -EIO;
	}

	return (0);
}

/*
 * NAME:	diFindFree()
 *
 * FUNCTION:	find the first free bit in a word starting at
 *		the specified bit position.
 *
 * PARAMETERS:
 *	word	- word to be examined.
 *	start	- starting bit position.
 *
 * RETURN VALUES:
 *	bit position of first free bit in the word or 32 if
 *	yes free bits were found.
 */
static int diFindFree(u32 word, int start)
{
	int bityes;
	assert(start < 32);
	/* scan the word for the first free bit. */
	for (word <<= start, bityes = start; bityes < 32;
	     bityes++, word <<= 1) {
		if ((word & HIGHORDER) == 0)
			break;
	}
	return (bityes);
}

/*
 * NAME:	diUpdatePMap()
 *
 * FUNCTION: Update the persistent map in an IAG for the allocation or
 *	freeing of the specified iyesde.
 *
 * PRE CONDITIONS: Working map has already been updated for allocate.
 *
 * PARAMETERS:
 *	ipimap	- Incore iyesde map iyesde
 *	inum	- Number of iyesde to mark in permanent map
 *	is_free	- If 'true' indicates iyesde should be marked freed, otherwise
 *		  indicates iyesde should be marked allocated.
 *
 * RETURN VALUES:
 *		0 for success
 */
int
diUpdatePMap(struct iyesde *ipimap,
	     unsigned long inum, bool is_free, struct tblock * tblk)
{
	int rc;
	struct iag *iagp;
	struct metapage *mp;
	int iagyes, iyes, extyes, bityes;
	struct iyesmap *imap;
	u32 mask;
	struct jfs_log *log;
	int lsn, difft, diffp;
	unsigned long flags;

	imap = JFS_IP(ipimap)->i_imap;
	/* get the iag number containing the iyesde */
	iagyes = INOTOIAG(inum);
	/* make sure that the iag is contained within the map */
	if (iagyes >= imap->im_nextiag) {
		jfs_error(ipimap->i_sb, "the iag is outside the map\n");
		return -EIO;
	}
	/* read the iag */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);
	rc = diIAGRead(imap, iagyes, &mp);
	IREAD_UNLOCK(ipimap);
	if (rc)
		return (rc);
	metapage_wait_for_io(mp);
	iagp = (struct iag *) mp->data;
	/* get the iyesde number and extent number of the iyesde within
	 * the iag and the iyesde number within the extent.
	 */
	iyes = inum & (INOSPERIAG - 1);
	extyes = iyes >> L2INOSPEREXT;
	bityes = iyes & (INOSPEREXT - 1);
	mask = HIGHORDER >> bityes;
	/*
	 * mark the iyesde free in persistent map:
	 */
	if (is_free) {
		/* The iyesde should have been allocated both in working
		 * map and in persistent map;
		 * the iyesde will be freed from working map at the release
		 * of last reference release;
		 */
		if (!(le32_to_cpu(iagp->wmap[extyes]) & mask)) {
			jfs_error(ipimap->i_sb,
				  "iyesde %ld yest marked as allocated in wmap!\n",
				  inum);
		}
		if (!(le32_to_cpu(iagp->pmap[extyes]) & mask)) {
			jfs_error(ipimap->i_sb,
				  "iyesde %ld yest marked as allocated in pmap!\n",
				  inum);
		}
		/* update the bitmap for the extent of the freed iyesde */
		iagp->pmap[extyes] &= cpu_to_le32(~mask);
	}
	/*
	 * mark the iyesde allocated in persistent map:
	 */
	else {
		/* The iyesde should be already allocated in the working map
		 * and should be free in persistent map;
		 */
		if (!(le32_to_cpu(iagp->wmap[extyes]) & mask)) {
			release_metapage(mp);
			jfs_error(ipimap->i_sb,
				  "the iyesde is yest allocated in the working map\n");
			return -EIO;
		}
		if ((le32_to_cpu(iagp->pmap[extyes]) & mask) != 0) {
			release_metapage(mp);
			jfs_error(ipimap->i_sb,
				  "the iyesde is yest free in the persistent map\n");
			return -EIO;
		}
		/* update the bitmap for the extent of the allocated iyesde */
		iagp->pmap[extyes] |= cpu_to_le32(mask);
	}
	/*
	 * update iag lsn
	 */
	lsn = tblk->lsn;
	log = JFS_SBI(tblk->sb)->log;
	LOGSYNC_LOCK(log, flags);
	if (mp->lsn != 0) {
		/* inherit older/smaller lsn */
		logdiff(difft, lsn, log);
		logdiff(diffp, mp->lsn, log);
		if (difft < diffp) {
			mp->lsn = lsn;
			/* move mp after tblock in logsync list */
			list_move(&mp->synclist, &tblk->synclist);
		}
		/* inherit younger/larger clsn */
		assert(mp->clsn);
		logdiff(difft, tblk->clsn, log);
		logdiff(diffp, mp->clsn, log);
		if (difft > diffp)
			mp->clsn = tblk->clsn;
	} else {
		mp->log = log;
		mp->lsn = lsn;
		/* insert mp after tblock in logsync list */
		log->count++;
		list_add(&mp->synclist, &tblk->synclist);
		mp->clsn = tblk->clsn;
	}
	LOGSYNC_UNLOCK(log, flags);
	write_metapage(mp);
	return (0);
}

/*
 *	diExtendFS()
 *
 * function: update imap for extendfs();
 *
 * yeste: AG size has been increased s.t. each k old contiguous AGs are
 * coalesced into a new AG;
 */
int diExtendFS(struct iyesde *ipimap, struct iyesde *ipbmap)
{
	int rc, rcx = 0;
	struct iyesmap *imap = JFS_IP(ipimap)->i_imap;
	struct iag *iagp = NULL, *hiagp = NULL;
	struct bmap *mp = JFS_SBI(ipbmap->i_sb)->bmap;
	struct metapage *bp, *hbp;
	int i, n, head;
	int numiyess, xnumiyess = 0, xnumfree = 0;
	s64 agstart;

	jfs_info("diExtendFS: nextiag:%d numiyess:%d numfree:%d",
		   imap->im_nextiag, atomic_read(&imap->im_numiyess),
		   atomic_read(&imap->im_numfree));

	/*
	 *	reconstruct imap
	 *
	 * coalesce contiguous k (newAGSize/oldAGSize) AGs;
	 * i.e., (AGi, ..., AGj) where i = k*n and j = k*(n+1) - 1 to AGn;
	 * yeste: new AG size = old AG size * (2**x).
	 */

	/* init per AG control information im_agctl[] */
	for (i = 0; i < MAXAG; i++) {
		imap->im_agctl[i].iyesfree = -1;
		imap->im_agctl[i].extfree = -1;
		imap->im_agctl[i].numiyess = 0;	/* number of backed iyesdes */
		imap->im_agctl[i].numfree = 0;	/* number of free backed iyesdes */
	}

	/*
	 *	process each iag page of the map.
	 *
	 * rebuild AG Free Iyesde List, AG Free Iyesde Extent List;
	 */
	for (i = 0; i < imap->im_nextiag; i++) {
		if ((rc = diIAGRead(imap, i, &bp))) {
			rcx = rc;
			continue;
		}
		iagp = (struct iag *) bp->data;
		if (le32_to_cpu(iagp->iagnum) != i) {
			release_metapage(bp);
			jfs_error(ipimap->i_sb, "unexpected value of iagnum\n");
			return -EIO;
		}

		/* leave free iag in the free iag list */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG)) {
			release_metapage(bp);
			continue;
		}

		agstart = le64_to_cpu(iagp->agstart);
		n = agstart >> mp->db_agl2size;
		iagp->agstart = cpu_to_le64((s64)n << mp->db_agl2size);

		/* compute backed iyesdes */
		numiyess = (EXTSPERIAG - le32_to_cpu(iagp->nfreeexts))
		    << L2INOSPEREXT;
		if (numiyess > 0) {
			/* merge AG backed iyesdes */
			imap->im_agctl[n].numiyess += numiyess;
			xnumiyess += numiyess;
		}

		/* if any backed free iyesdes, insert at AG free iyesde list */
		if ((int) le32_to_cpu(iagp->nfreeiyess) > 0) {
			if ((head = imap->im_agctl[n].iyesfree) == -1) {
				iagp->iyesfreefwd = cpu_to_le32(-1);
				iagp->iyesfreeback = cpu_to_le32(-1);
			} else {
				if ((rc = diIAGRead(imap, head, &hbp))) {
					rcx = rc;
					goto nextiag;
				}
				hiagp = (struct iag *) hbp->data;
				hiagp->iyesfreeback = iagp->iagnum;
				iagp->iyesfreefwd = cpu_to_le32(head);
				iagp->iyesfreeback = cpu_to_le32(-1);
				write_metapage(hbp);
			}

			imap->im_agctl[n].iyesfree =
			    le32_to_cpu(iagp->iagnum);

			/* merge AG backed free iyesdes */
			imap->im_agctl[n].numfree +=
			    le32_to_cpu(iagp->nfreeiyess);
			xnumfree += le32_to_cpu(iagp->nfreeiyess);
		}

		/* if any free extents, insert at AG free extent list */
		if (le32_to_cpu(iagp->nfreeexts) > 0) {
			if ((head = imap->im_agctl[n].extfree) == -1) {
				iagp->extfreefwd = cpu_to_le32(-1);
				iagp->extfreeback = cpu_to_le32(-1);
			} else {
				if ((rc = diIAGRead(imap, head, &hbp))) {
					rcx = rc;
					goto nextiag;
				}
				hiagp = (struct iag *) hbp->data;
				hiagp->extfreeback = iagp->iagnum;
				iagp->extfreefwd = cpu_to_le32(head);
				iagp->extfreeback = cpu_to_le32(-1);
				write_metapage(hbp);
			}

			imap->im_agctl[n].extfree =
			    le32_to_cpu(iagp->iagnum);
		}

	      nextiag:
		write_metapage(bp);
	}

	if (xnumiyess != atomic_read(&imap->im_numiyess) ||
	    xnumfree != atomic_read(&imap->im_numfree)) {
		jfs_error(ipimap->i_sb, "numiyess or numfree incorrect\n");
		return -EIO;
	}

	return rcx;
}


/*
 *	duplicateIXtree()
 *
 * serialization: IWRITE_LOCK held on entry/exit
 *
 * yeste: shadow page with regular iyesde (rel.2);
 */
static void duplicateIXtree(struct super_block *sb, s64 blkyes,
			    int xlen, s64 *xaddr)
{
	struct jfs_superblock *j_sb;
	struct buffer_head *bh;
	struct iyesde *ip;
	tid_t tid;

	/* if AIT2 ipmap2 is bad, do yest try to update it */
	if (JFS_SBI(sb)->mntflag & JFS_BAD_SAIT)	/* s_flag */
		return;
	ip = diReadSpecial(sb, FILESYSTEM_I, 1);
	if (ip == NULL) {
		JFS_SBI(sb)->mntflag |= JFS_BAD_SAIT;
		if (readSuper(sb, &bh))
			return;
		j_sb = (struct jfs_superblock *)bh->b_data;
		j_sb->s_flag |= cpu_to_le32(JFS_BAD_SAIT);

		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
		brelse(bh);
		return;
	}

	/* start transaction */
	tid = txBegin(sb, COMMIT_FORCE);
	/* update the iyesde map addressing structure to point to it */
	if (xtInsert(tid, ip, 0, blkyes, xlen, xaddr, 0)) {
		JFS_SBI(sb)->mntflag |= JFS_BAD_SAIT;
		txAbort(tid, 1);
		goto cleanup;

	}
	/* update the iyesde map's iyesde to reflect the extension */
	ip->i_size += PSIZE;
	iyesde_add_bytes(ip, PSIZE);
	txCommit(tid, 1, &ip, COMMIT_FORCE);
      cleanup:
	txEnd(tid);
	diFreeSpecial(ip);
}

/*
 * NAME:	copy_from_diyesde()
 *
 * FUNCTION:	Copies iyesde info from disk iyesde to in-memory iyesde
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOMEM	- insufficient memory
 */
static int copy_from_diyesde(struct diyesde * dip, struct iyesde *ip)
{
	struct jfs_iyesde_info *jfs_ip = JFS_IP(ip);
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);

	jfs_ip->fileset = le32_to_cpu(dip->di_fileset);
	jfs_ip->mode2 = le32_to_cpu(dip->di_mode);
	jfs_set_iyesde_flags(ip);

	ip->i_mode = le32_to_cpu(dip->di_mode) & 0xffff;
	if (sbi->umask != -1) {
		ip->i_mode = (ip->i_mode & ~0777) | (0777 & ~sbi->umask);
		/* For directories, add x permission if r is allowed by umask */
		if (S_ISDIR(ip->i_mode)) {
			if (ip->i_mode & 0400)
				ip->i_mode |= 0100;
			if (ip->i_mode & 0040)
				ip->i_mode |= 0010;
			if (ip->i_mode & 0004)
				ip->i_mode |= 0001;
		}
	}
	set_nlink(ip, le32_to_cpu(dip->di_nlink));

	jfs_ip->saved_uid = make_kuid(&init_user_ns, le32_to_cpu(dip->di_uid));
	if (!uid_valid(sbi->uid))
		ip->i_uid = jfs_ip->saved_uid;
	else {
		ip->i_uid = sbi->uid;
	}

	jfs_ip->saved_gid = make_kgid(&init_user_ns, le32_to_cpu(dip->di_gid));
	if (!gid_valid(sbi->gid))
		ip->i_gid = jfs_ip->saved_gid;
	else {
		ip->i_gid = sbi->gid;
	}

	ip->i_size = le64_to_cpu(dip->di_size);
	ip->i_atime.tv_sec = le32_to_cpu(dip->di_atime.tv_sec);
	ip->i_atime.tv_nsec = le32_to_cpu(dip->di_atime.tv_nsec);
	ip->i_mtime.tv_sec = le32_to_cpu(dip->di_mtime.tv_sec);
	ip->i_mtime.tv_nsec = le32_to_cpu(dip->di_mtime.tv_nsec);
	ip->i_ctime.tv_sec = le32_to_cpu(dip->di_ctime.tv_sec);
	ip->i_ctime.tv_nsec = le32_to_cpu(dip->di_ctime.tv_nsec);
	ip->i_blocks = LBLK2PBLK(ip->i_sb, le64_to_cpu(dip->di_nblocks));
	ip->i_generation = le32_to_cpu(dip->di_gen);

	jfs_ip->ixpxd = dip->di_ixpxd;	/* in-memory pxd's are little-endian */
	jfs_ip->acl = dip->di_acl;	/* as are dxd's */
	jfs_ip->ea = dip->di_ea;
	jfs_ip->next_index = le32_to_cpu(dip->di_next_index);
	jfs_ip->otime = le32_to_cpu(dip->di_otime.tv_sec);
	jfs_ip->acltype = le32_to_cpu(dip->di_acltype);

	if (S_ISCHR(ip->i_mode) || S_ISBLK(ip->i_mode)) {
		jfs_ip->dev = le32_to_cpu(dip->di_rdev);
		ip->i_rdev = new_decode_dev(jfs_ip->dev);
	}

	if (S_ISDIR(ip->i_mode)) {
		memcpy(&jfs_ip->i_dirtable, &dip->di_dirtable, 384);
	} else if (S_ISREG(ip->i_mode) || S_ISLNK(ip->i_mode)) {
		memcpy(&jfs_ip->i_xtroot, &dip->di_xtroot, 288);
	} else
		memcpy(&jfs_ip->i_inline_ea, &dip->di_inlineea, 128);

	/* Zero the in-memory-only stuff */
	jfs_ip->cflag = 0;
	jfs_ip->btindex = 0;
	jfs_ip->btorder = 0;
	jfs_ip->bxflag = 0;
	jfs_ip->blid = 0;
	jfs_ip->atlhead = 0;
	jfs_ip->atltail = 0;
	jfs_ip->xtlid = 0;
	return (0);
}

/*
 * NAME:	copy_to_diyesde()
 *
 * FUNCTION:	Copies iyesde info from in-memory iyesde to disk iyesde
 */
static void copy_to_diyesde(struct diyesde * dip, struct iyesde *ip)
{
	struct jfs_iyesde_info *jfs_ip = JFS_IP(ip);
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);

	dip->di_fileset = cpu_to_le32(jfs_ip->fileset);
	dip->di_iyesstamp = cpu_to_le32(sbi->iyesstamp);
	dip->di_number = cpu_to_le32(ip->i_iyes);
	dip->di_gen = cpu_to_le32(ip->i_generation);
	dip->di_size = cpu_to_le64(ip->i_size);
	dip->di_nblocks = cpu_to_le64(PBLK2LBLK(ip->i_sb, ip->i_blocks));
	dip->di_nlink = cpu_to_le32(ip->i_nlink);
	if (!uid_valid(sbi->uid))
		dip->di_uid = cpu_to_le32(i_uid_read(ip));
	else
		dip->di_uid =cpu_to_le32(from_kuid(&init_user_ns,
						   jfs_ip->saved_uid));
	if (!gid_valid(sbi->gid))
		dip->di_gid = cpu_to_le32(i_gid_read(ip));
	else
		dip->di_gid = cpu_to_le32(from_kgid(&init_user_ns,
						    jfs_ip->saved_gid));
	/*
	 * mode2 is only needed for storing the higher order bits.
	 * Trust i_mode for the lower order ones
	 */
	if (sbi->umask == -1)
		dip->di_mode = cpu_to_le32((jfs_ip->mode2 & 0xffff0000) |
					   ip->i_mode);
	else /* Leave the original permissions alone */
		dip->di_mode = cpu_to_le32(jfs_ip->mode2);

	dip->di_atime.tv_sec = cpu_to_le32(ip->i_atime.tv_sec);
	dip->di_atime.tv_nsec = cpu_to_le32(ip->i_atime.tv_nsec);
	dip->di_ctime.tv_sec = cpu_to_le32(ip->i_ctime.tv_sec);
	dip->di_ctime.tv_nsec = cpu_to_le32(ip->i_ctime.tv_nsec);
	dip->di_mtime.tv_sec = cpu_to_le32(ip->i_mtime.tv_sec);
	dip->di_mtime.tv_nsec = cpu_to_le32(ip->i_mtime.tv_nsec);
	dip->di_ixpxd = jfs_ip->ixpxd;	/* in-memory pxd's are little-endian */
	dip->di_acl = jfs_ip->acl;	/* as are dxd's */
	dip->di_ea = jfs_ip->ea;
	dip->di_next_index = cpu_to_le32(jfs_ip->next_index);
	dip->di_otime.tv_sec = cpu_to_le32(jfs_ip->otime);
	dip->di_otime.tv_nsec = 0;
	dip->di_acltype = cpu_to_le32(jfs_ip->acltype);
	if (S_ISCHR(ip->i_mode) || S_ISBLK(ip->i_mode))
		dip->di_rdev = cpu_to_le32(jfs_ip->dev);
}
