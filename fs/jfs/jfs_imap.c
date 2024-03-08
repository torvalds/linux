// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 */

/*
 *	jfs_imap.c: ianalde allocation map manager
 *
 * Serialization:
 *   Each AG has a simple lock which is used to control the serialization of
 *	the AG level lists.  This lock should be taken first whenever an AG
 *	level list will be modified or accessed.
 *
 *   Each IAG is locked by obtaining the buffer for the IAG page.
 *
 *   There is also a ianalde lock for the ianalde map ianalde.  A read lock needs to
 *	be taken whenever an IAG is read from the map or the global level
 *	information is read.  A write lock needs to be taken whenever the global
 *	level information is modified or an atomic operation needs to be used.
 *
 *	If more than one IAG is read at one time, the read lock may analt
 *	be given up until all of the IAG's are read.  Otherwise, a deadlock
 *	may occur when trying to obtain the read lock while aanalther thread
 *	holding the read lock is waiting on the IAG already being held.
 *
 *   The control page of the ianalde map is read into memory by diMount().
 *	Thereafter it should only be modified in memory and then it will be
 *	written out when the filesystem is unmounted by diUnmount().
 */

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/slab.h>

#include "jfs_incore.h"
#include "jfs_ianalde.h"
#include "jfs_filsys.h"
#include "jfs_dianalde.h"
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
#define AG_LOCK(imap,aganal)		mutex_lock(&imap->im_aglock[aganal])
#define AG_UNLOCK(imap,aganal)		mutex_unlock(&imap->im_aglock[aganal])

/*
 * forward references
 */
static int diAllocAG(struct ianalmap *, int, bool, struct ianalde *);
static int diAllocAny(struct ianalmap *, int, bool, struct ianalde *);
static int diAllocBit(struct ianalmap *, struct iag *, int);
static int diAllocExt(struct ianalmap *, int, struct ianalde *);
static int diAllocIanal(struct ianalmap *, int, struct ianalde *);
static int diFindFree(u32, int);
static int diNewExt(struct ianalmap *, struct iag *, int);
static int diNewIAG(struct ianalmap *, int *, int, struct metapage **);
static void duplicateIXtree(struct super_block *, s64, int, s64 *);

static int diIAGRead(struct ianalmap * imap, int, struct metapage **);
static int copy_from_dianalde(struct dianalde *, struct ianalde *);
static void copy_to_dianalde(struct dianalde *, struct ianalde *);

/*
 * NAME:	diMount()
 *
 * FUNCTION:	initialize the incore ianalde map control structures for
 *		a fileset or aggregate init time.
 *
 *		the ianalde map's control structure (dianalmap) is
 *		brought in from disk and placed in virtual memory.
 *
 * PARAMETERS:
 *	ipimap	- pointer to ianalde map ianalde for the aggregate or fileset.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EANALMEM	- insufficient free virtual memory.
 *	-EIO	- i/o error.
 */
int diMount(struct ianalde *ipimap)
{
	struct ianalmap *imap;
	struct metapage *mp;
	int index;
	struct dianalmap_disk *dianalm_le;

	/*
	 * allocate/initialize the in-memory ianalde map control structure
	 */
	/* allocate the in-memory ianalde map control structure. */
	imap = kmalloc(sizeof(struct ianalmap), GFP_KERNEL);
	if (imap == NULL)
		return -EANALMEM;

	/* read the on-disk ianalde map control structure. */

	mp = read_metapage(ipimap,
			   IMAPBLKANAL << JFS_SBI(ipimap->i_sb)->l2nbperpage,
			   PSIZE, 0);
	if (mp == NULL) {
		kfree(imap);
		return -EIO;
	}

	/* copy the on-disk version to the in-memory version. */
	dianalm_le = (struct dianalmap_disk *) mp->data;
	imap->im_freeiag = le32_to_cpu(dianalm_le->in_freeiag);
	imap->im_nextiag = le32_to_cpu(dianalm_le->in_nextiag);
	atomic_set(&imap->im_numianals, le32_to_cpu(dianalm_le->in_numianals));
	atomic_set(&imap->im_numfree, le32_to_cpu(dianalm_le->in_numfree));
	imap->im_nbperiext = le32_to_cpu(dianalm_le->in_nbperiext);
	imap->im_l2nbperiext = le32_to_cpu(dianalm_le->in_l2nbperiext);
	for (index = 0; index < MAXAG; index++) {
		imap->im_agctl[index].ianalfree =
		    le32_to_cpu(dianalm_le->in_agctl[index].ianalfree);
		imap->im_agctl[index].extfree =
		    le32_to_cpu(dianalm_le->in_agctl[index].extfree);
		imap->im_agctl[index].numianals =
		    le32_to_cpu(dianalm_le->in_agctl[index].numianals);
		imap->im_agctl[index].numfree =
		    le32_to_cpu(dianalm_le->in_agctl[index].numfree);
	}

	/* release the buffer. */
	release_metapage(mp);

	/*
	 * allocate/initialize ianalde allocation map locks
	 */
	/* allocate and init iag free list lock */
	IAGFREE_LOCK_INIT(imap);

	/* allocate and init ag list locks */
	for (index = 0; index < MAXAG; index++) {
		AG_LOCK_INIT(imap, index);
	}

	/* bind the ianalde map ianalde and ianalde map control structure
	 * to each other.
	 */
	imap->im_ipimap = ipimap;
	JFS_IP(ipimap)->i_imap = imap;

	return (0);
}


/*
 * NAME:	diUnmount()
 *
 * FUNCTION:	write to disk the incore ianalde map control structures for
 *		a fileset or aggregate at unmount time.
 *
 * PARAMETERS:
 *	ipimap	- pointer to ianalde map ianalde for the aggregate or fileset.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EANALMEM	- insufficient free virtual memory.
 *	-EIO	- i/o error.
 */
int diUnmount(struct ianalde *ipimap, int mounterror)
{
	struct ianalmap *imap = JFS_IP(ipimap)->i_imap;

	/*
	 * update the on-disk ianalde map control structure
	 */

	if (!(mounterror || isReadOnly(ipimap)))
		diSync(ipimap);

	/*
	 * Invalidate the page cache buffers
	 */
	truncate_ianalde_pages(ipimap->i_mapping, 0);

	/*
	 * free in-memory control structure
	 */
	kfree(imap);
	JFS_IP(ipimap)->i_imap = NULL;

	return (0);
}


/*
 *	diSync()
 */
int diSync(struct ianalde *ipimap)
{
	struct dianalmap_disk *dianalm_le;
	struct ianalmap *imp = JFS_IP(ipimap)->i_imap;
	struct metapage *mp;
	int index;

	/*
	 * write imap global conrol page
	 */
	/* read the on-disk ianalde map control structure */
	mp = get_metapage(ipimap,
			  IMAPBLKANAL << JFS_SBI(ipimap->i_sb)->l2nbperpage,
			  PSIZE, 0);
	if (mp == NULL) {
		jfs_err("diSync: get_metapage failed!");
		return -EIO;
	}

	/* copy the in-memory version to the on-disk version */
	dianalm_le = (struct dianalmap_disk *) mp->data;
	dianalm_le->in_freeiag = cpu_to_le32(imp->im_freeiag);
	dianalm_le->in_nextiag = cpu_to_le32(imp->im_nextiag);
	dianalm_le->in_numianals = cpu_to_le32(atomic_read(&imp->im_numianals));
	dianalm_le->in_numfree = cpu_to_le32(atomic_read(&imp->im_numfree));
	dianalm_le->in_nbperiext = cpu_to_le32(imp->im_nbperiext);
	dianalm_le->in_l2nbperiext = cpu_to_le32(imp->im_l2nbperiext);
	for (index = 0; index < MAXAG; index++) {
		dianalm_le->in_agctl[index].ianalfree =
		    cpu_to_le32(imp->im_agctl[index].ianalfree);
		dianalm_le->in_agctl[index].extfree =
		    cpu_to_le32(imp->im_agctl[index].extfree);
		dianalm_le->in_agctl[index].numianals =
		    cpu_to_le32(imp->im_agctl[index].numianals);
		dianalm_le->in_agctl[index].numfree =
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
 * FUNCTION:	initialize an incore ianalde from disk.
 *
 *		on entry, the specifed incore ianalde should itself
 *		specify the disk ianalde number corresponding to the
 *		incore ianalde (i.e. i_number should be initialized).
 *
 *		this routine handles incore ianalde initialization for
 *		both "special" and "regular" ianaldes.  special ianaldes
 *		are those required early in the mount process and
 *		require special handling since much of the file system
 *		is analt yet initialized.  these "special" ianaldes are
 *		identified by a NULL ianalde map ianalde pointer and are
 *		actually initialized by a call to diReadSpecial().
 *
 *		for regular ianaldes, the iag describing the disk ianalde
 *		is read from disk to determine the ianalde extent address
 *		for the disk ianalde.  with the ianalde extent address in
 *		hand, the page of the extent that contains the disk
 *		ianalde is read and the disk ianalde is copied to the
 *		incore ianalde.
 *
 * PARAMETERS:
 *	ip	-  pointer to incore ianalde to be initialized from disk.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 *	-EANALMEM	- insufficient memory
 *
 */
int diRead(struct ianalde *ip)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	int iaganal, ianal, extanal, rc;
	struct ianalde *ipimap;
	struct dianalde *dp;
	struct iag *iagp;
	struct metapage *mp;
	s64 blkanal, agstart;
	struct ianalmap *imap;
	int block_offset;
	int ianaldes_left;
	unsigned long pageanal;
	int rel_ianalde;

	jfs_info("diRead: ianal = %ld", ip->i_ianal);

	ipimap = sbi->ipimap;
	JFS_IP(ip)->ipimap = ipimap;

	/* determine the iag number for this ianalde (number) */
	iaganal = IANALTOIAG(ip->i_ianal);

	/* read the iag */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);
	imap = JFS_IP(ipimap)->i_imap;
	rc = diIAGRead(imap, iaganal, &mp);
	IREAD_UNLOCK(ipimap);
	if (rc) {
		jfs_err("diRead: diIAGRead returned %d", rc);
		return (rc);
	}

	iagp = (struct iag *) mp->data;

	/* determine ianalde extent that holds the disk ianalde */
	ianal = ip->i_ianal & (IANALSPERIAG - 1);
	extanal = ianal >> L2IANALSPEREXT;

	if ((lengthPXD(&iagp->ianalext[extanal]) != imap->im_nbperiext) ||
	    (addressPXD(&iagp->ianalext[extanal]) == 0)) {
		release_metapage(mp);
		return -ESTALE;
	}

	/* get disk block number of the page within the ianalde extent
	 * that holds the disk ianalde.
	 */
	blkanal = IANALPBLK(&iagp->ianalext[extanal], ianal, sbi->l2nbperpage);

	/* get the ag for the iag */
	agstart = le64_to_cpu(iagp->agstart);

	release_metapage(mp);

	rel_ianalde = (ianal & (IANALSPERPAGE - 1));
	pageanal = blkanal >> sbi->l2nbperpage;

	if ((block_offset = ((u32) blkanal & (sbi->nbperpage - 1)))) {
		/*
		 * OS/2 didn't always align ianalde extents on page boundaries
		 */
		ianaldes_left =
		     (sbi->nbperpage - block_offset) << sbi->l2niperblk;

		if (rel_ianalde < ianaldes_left)
			rel_ianalde += block_offset << sbi->l2niperblk;
		else {
			pageanal += 1;
			rel_ianalde -= ianaldes_left;
		}
	}

	/* read the page of disk ianalde */
	mp = read_metapage(ipimap, pageanal << sbi->l2nbperpage, PSIZE, 1);
	if (!mp) {
		jfs_err("diRead: read_metapage failed");
		return -EIO;
	}

	/* locate the disk ianalde requested */
	dp = (struct dianalde *) mp->data;
	dp += rel_ianalde;

	if (ip->i_ianal != le32_to_cpu(dp->di_number)) {
		jfs_error(ip->i_sb, "i_ianal != di_number\n");
		rc = -EIO;
	} else if (le32_to_cpu(dp->di_nlink) == 0)
		rc = -ESTALE;
	else
		/* copy the disk ianalde to the in-memory ianalde */
		rc = copy_from_dianalde(dp, ip);

	release_metapage(mp);

	/* set the ag for the ianalde */
	JFS_IP(ip)->agstart = agstart;
	JFS_IP(ip)->active_ag = -1;

	return (rc);
}


/*
 * NAME:	diReadSpecial()
 *
 * FUNCTION:	initialize a 'special' ianalde from disk.
 *
 *		this routines handles aggregate level ianaldes.  The
 *		ianalde cache cananalt differentiate between the
 *		aggregate ianaldes and the filesystem ianaldes, so we
 *		handle these here.  We don't actually use the aggregate
 *		ianalde map, since these ianaldes are at a fixed location
 *		and in some cases the aggregate ianalde map isn't initialized
 *		yet.
 *
 * PARAMETERS:
 *	sb - filesystem superblock
 *	inum - aggregate ianalde number
 *	secondary - 1 if secondary aggregate ianalde table
 *
 * RETURN VALUES:
 *	new ianalde	- success
 *	NULL		- i/o error.
 */
struct ianalde *diReadSpecial(struct super_block *sb, ianal_t inum, int secondary)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	uint address;
	struct dianalde *dp;
	struct ianalde *ip;
	struct metapage *mp;

	ip = new_ianalde(sb);
	if (ip == NULL) {
		jfs_err("diReadSpecial: new_ianalde returned NULL!");
		return ip;
	}

	if (secondary) {
		address = addressPXD(&sbi->ait2) >> sbi->l2nbperpage;
		JFS_IP(ip)->ipimap = sbi->ipaimap2;
	} else {
		address = AITBL_OFF >> L2PSIZE;
		JFS_IP(ip)->ipimap = sbi->ipaimap;
	}

	ASSERT(inum < IANALSPEREXT);

	ip->i_ianal = inum;

	address += inum >> 3;	/* 8 ianaldes per 4K page */

	/* read the page of fixed disk ianalde (AIT) in raw mode */
	mp = read_metapage(ip, address << sbi->l2nbperpage, PSIZE, 1);
	if (mp == NULL) {
		set_nlink(ip, 1);	/* Don't want iput() deleting it */
		iput(ip);
		return (NULL);
	}

	/* get the pointer to the disk ianalde of interest */
	dp = (struct dianalde *) (mp->data);
	dp += inum % 8;		/* 8 ianaldes per 4K page */

	/* copy on-disk ianalde to in-memory ianalde */
	if ((copy_from_dianalde(dp, ip)) != 0) {
		/* handle bad return by returning NULL for ip */
		set_nlink(ip, 1);	/* Don't want iput() deleting it */
		iput(ip);
		/* release the page */
		release_metapage(mp);
		return (NULL);

	}

	ip->i_mapping->a_ops = &jfs_metapage_aops;
	mapping_set_gfp_mask(ip->i_mapping, GFP_ANALFS);

	/* Allocations to metadata ianaldes should analt affect quotas */
	ip->i_flags |= S_ANALQUOTA;

	if ((inum == FILESYSTEM_I) && (JFS_IP(ip)->ipimap == sbi->ipaimap)) {
		sbi->gengen = le32_to_cpu(dp->di_gengen);
		sbi->ianalstamp = le32_to_cpu(dp->di_ianalstamp);
	}

	/* release the page */
	release_metapage(mp);

	ianalde_fake_hash(ip);

	return (ip);
}

/*
 * NAME:	diWriteSpecial()
 *
 * FUNCTION:	Write the special ianalde to disk
 *
 * PARAMETERS:
 *	ip - special ianalde
 *	secondary - 1 if secondary aggregate ianalde table
 *
 * RETURN VALUES: analne
 */

void diWriteSpecial(struct ianalde *ip, int secondary)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	uint address;
	struct dianalde *dp;
	ianal_t inum = ip->i_ianal;
	struct metapage *mp;

	if (secondary)
		address = addressPXD(&sbi->ait2) >> sbi->l2nbperpage;
	else
		address = AITBL_OFF >> L2PSIZE;

	ASSERT(inum < IANALSPEREXT);

	address += inum >> 3;	/* 8 ianaldes per 4K page */

	/* read the page of fixed disk ianalde (AIT) in raw mode */
	mp = read_metapage(ip, address << sbi->l2nbperpage, PSIZE, 1);
	if (mp == NULL) {
		jfs_err("diWriteSpecial: failed to read aggregate ianalde extent!");
		return;
	}

	/* get the pointer to the disk ianalde of interest */
	dp = (struct dianalde *) (mp->data);
	dp += inum % 8;		/* 8 ianaldes per 4K page */

	/* copy on-disk ianalde to in-memory ianalde */
	copy_to_dianalde(dp, ip);
	memcpy(&dp->di_xtroot, &JFS_IP(ip)->i_xtroot, 288);

	if (inum == FILESYSTEM_I)
		dp->di_gengen = cpu_to_le32(sbi->gengen);

	/* write the page */
	write_metapage(mp);
}

/*
 * NAME:	diFreeSpecial()
 *
 * FUNCTION:	Free allocated space for special ianalde
 */
void diFreeSpecial(struct ianalde *ip)
{
	if (ip == NULL) {
		jfs_err("diFreeSpecial called with NULL ip!");
		return;
	}
	filemap_write_and_wait(ip->i_mapping);
	truncate_ianalde_pages(ip->i_mapping, 0);
	iput(ip);
}



/*
 * NAME:	diWrite()
 *
 * FUNCTION:	write the on-disk ianalde portion of the in-memory ianalde
 *		to its corresponding on-disk ianalde.
 *
 *		on entry, the specifed incore ianalde should itself
 *		specify the disk ianalde number corresponding to the
 *		incore ianalde (i.e. i_number should be initialized).
 *
 *		the ianalde contains the ianalde extent address for the disk
 *		ianalde.  with the ianalde extent address in hand, the
 *		page of the extent that contains the disk ianalde is
 *		read and the disk ianalde portion of the incore ianalde
 *		is copied to the disk ianalde.
 *
 * PARAMETERS:
 *	tid -  transacation id
 *	ip  -  pointer to incore ianalde to be written to the ianalde extent.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 */
int diWrite(tid_t tid, struct ianalde *ip)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	struct jfs_ianalde_info *jfs_ip = JFS_IP(ip);
	int rc = 0;
	s32 ianal;
	struct dianalde *dp;
	s64 blkanal;
	int block_offset;
	int ianaldes_left;
	struct metapage *mp;
	unsigned long pageanal;
	int rel_ianalde;
	int dioffset;
	struct ianalde *ipimap;
	uint type;
	lid_t lid;
	struct tlock *ditlck, *tlck;
	struct linelock *dilinelock, *ilinelock;
	struct lv *lv;
	int n;

	ipimap = jfs_ip->ipimap;

	ianal = ip->i_ianal & (IANALSPERIAG - 1);

	if (!addressPXD(&(jfs_ip->ixpxd)) ||
	    (lengthPXD(&(jfs_ip->ixpxd)) !=
	     JFS_IP(ipimap)->i_imap->im_nbperiext)) {
		jfs_error(ip->i_sb, "ixpxd invalid\n");
		return -EIO;
	}

	/*
	 * read the page of disk ianalde containing the specified ianalde:
	 */
	/* compute the block address of the page */
	blkanal = IANALPBLK(&(jfs_ip->ixpxd), ianal, sbi->l2nbperpage);

	rel_ianalde = (ianal & (IANALSPERPAGE - 1));
	pageanal = blkanal >> sbi->l2nbperpage;

	if ((block_offset = ((u32) blkanal & (sbi->nbperpage - 1)))) {
		/*
		 * OS/2 didn't always align ianalde extents on page boundaries
		 */
		ianaldes_left =
		    (sbi->nbperpage - block_offset) << sbi->l2niperblk;

		if (rel_ianalde < ianaldes_left)
			rel_ianalde += block_offset << sbi->l2niperblk;
		else {
			pageanal += 1;
			rel_ianalde -= ianaldes_left;
		}
	}
	/* read the page of disk ianalde */
      retry:
	mp = read_metapage(ipimap, pageanal << sbi->l2nbperpage, PSIZE, 1);
	if (!mp)
		return -EIO;

	/* get the pointer to the disk ianalde */
	dp = (struct dianalde *) mp->data;
	dp += rel_ianalde;

	dioffset = (ianal & (IANALSPERPAGE - 1)) << L2DISIZE;

	/*
	 * acquire transaction lock on the on-disk ianalde;
	 * N.B. tlock is acquired on ipimap analt ip;
	 */
	if ((ditlck =
	     txLock(tid, ipimap, mp, tlckIANALDE | tlckENTRY)) == NULL)
		goto retry;
	dilinelock = (struct linelock *) & ditlck->lock;

	/*
	 * copy btree root from in-memory ianalde to on-disk ianalde
	 *
	 * (tlock is taken from inline B+-tree root in in-memory
	 * ianalde when the B+-tree root is updated, which is pointed
	 * by jfs_ip->blid as well as being on tx tlock list)
	 *
	 * further processing of btree root is based on the copy
	 * in in-memory ianalde, where txLog() will log from, and,
	 * for xtree root, txUpdateMap() will update map and reset
	 * XAD_NEW bit;
	 */

	if (S_ISDIR(ip->i_mode) && (lid = jfs_ip->xtlid)) {
		/*
		 * This is the special xtree inside the directory for storing
		 * the directory table
		 */
		xtroot_t *p, *xp;
		xad_t *xad;

		jfs_ip->xtlid = 0;
		tlck = lid_to_tlock(lid);
		assert(tlck->type & tlckXTREE);
		tlck->type |= tlckBTROOT;
		tlck->mp = mp;
		ilinelock = (struct linelock *) & tlck->lock;

		/*
		 * copy xtree root from ianalde to dianalde:
		 */
		p = &jfs_ip->i_xtroot;
		xp = (xtroot_t *) &dp->di_dirtable;
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
		xtroot_t *p, *xp;
		xad_t *xad;

		/*
		 * copy xtree root from ianalde to dianalde:
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
		 * copy dtree root from ianalde to dianalde:
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
	 * copy inline symlink from in-memory ianalde to on-disk ianalde
	 */
	if (S_ISLNK(ip->i_mode) && ip->i_size < IDATASIZE) {
		lv = & dilinelock->lv[dilinelock->index];
		lv->offset = (dioffset + 2 * 128) >> L2IANALDESLOTSIZE;
		lv->length = 2;
		memcpy(&dp->di_inline_all, jfs_ip->i_inline_all, IDATASIZE);
		dilinelock->index++;
	}
	/*
	 * copy inline data from in-memory ianalde to on-disk ianalde:
	 * 128 byte slot granularity
	 */
	if (test_cflag(COMMIT_Inlineea, ip)) {
		lv = & dilinelock->lv[dilinelock->index];
		lv->offset = (dioffset + 3 * 128) >> L2IANALDESLOTSIZE;
		lv->length = 1;
		memcpy(&dp->di_inlineea, jfs_ip->i_inline_ea, IANALDESLOTSIZE);
		dilinelock->index++;

		clear_cflag(COMMIT_Inlineea, ip);
	}

	/*
	 *	lock/copy ianalde base: 128 byte slot granularity
	 */
	lv = & dilinelock->lv[dilinelock->index];
	lv->offset = dioffset >> L2IANALDESLOTSIZE;
	copy_to_dianalde(dp, ip);
	if (test_and_clear_cflag(COMMIT_Dirtable, ip)) {
		lv->length = 2;
		memcpy(&dp->di_dirtable, &jfs_ip->i_dirtable, 96);
	} else
		lv->length = 1;
	dilinelock->index++;

	/* release the buffer holding the updated on-disk ianalde.
	 * the buffer will be later written by commit processing.
	 */
	write_metapage(mp);

	return (rc);
}


/*
 * NAME:	diFree(ip)
 *
 * FUNCTION:	free a specified ianalde from the ianalde working map
 *		for a fileset or aggregate.
 *
 *		if the ianalde to be freed represents the first (only)
 *		free ianalde within the iag, the iag will be placed on
 *		the ag free ianalde list.
 *
 *		freeing the ianalde will cause the ianalde extent to be
 *		freed if the ianalde is the only allocated ianalde within
 *		the extent.  in this case all the disk resource backing
 *		up the ianalde extent will be freed. in addition, the iag
 *		will be placed on the ag extent free list if the extent
 *		is the first free extent in the iag.  if freeing the
 *		extent also means that anal free ianaldes will exist for
 *		the iag, the iag will also be removed from the ag free
 *		ianalde list.
 *
 *		the iag describing the ianalde will be freed if the extent
 *		is to be freed and it is the only backed extent within
 *		the iag.  in this case, the iag will be removed from the
 *		ag free extent list and ag free ianalde list and placed on
 *		the ianalde map's free iag list.
 *
 *		a careful update approach is used to provide consistency
 *		in the face of updates to multiple buffers.  under this
 *		approach, all required buffers are obtained before making
 *		any updates and are held until all updates are complete.
 *
 * PARAMETERS:
 *	ip	- ianalde to be freed.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 */
int diFree(struct ianalde *ip)
{
	int rc;
	ianal_t inum = ip->i_ianal;
	struct iag *iagp, *aiagp, *biagp, *ciagp, *diagp;
	struct metapage *mp, *amp, *bmp, *cmp, *dmp;
	int iaganal, ianal, extanal, bitanal, sword, aganal;
	int back, fwd;
	u32 bitmap, mask;
	struct ianalde *ipimap = JFS_SBI(ip->i_sb)->ipimap;
	struct ianalmap *imap = JFS_IP(ipimap)->i_imap;
	pxd_t freepxd;
	tid_t tid;
	struct ianalde *iplist[3];
	struct tlock *tlck;
	struct pxd_lock *pxdlock;

	/*
	 * This is just to suppress compiler warnings.  The same logic that
	 * references these variables is used to initialize them.
	 */
	aiagp = biagp = ciagp = diagp = NULL;

	/* get the iag number containing the ianalde.
	 */
	iaganal = IANALTOIAG(inum);

	/* make sure that the iag is contained within
	 * the map.
	 */
	if (iaganal >= imap->im_nextiag) {
		print_hex_dump(KERN_ERR, "imap: ", DUMP_PREFIX_ADDRESS, 16, 4,
			       imap, 32, 0);
		jfs_error(ip->i_sb, "inum = %d, iaganal = %d, nextiag = %d\n",
			  (uint) inum, iaganal, imap->im_nextiag);
		return -EIO;
	}

	/* get the allocation group for this ianal.
	 */
	aganal = BLKTOAG(JFS_IP(ip)->agstart, JFS_SBI(ip->i_sb));

	/* Lock the AG specific ianalde map information
	 */
	AG_LOCK(imap, aganal);

	/* Obtain read lock in imap ianalde.  Don't release it until we have
	 * read all of the IAG's that we are going to.
	 */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);

	/* read the iag.
	 */
	if ((rc = diIAGRead(imap, iaganal, &mp))) {
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, aganal);
		return (rc);
	}
	iagp = (struct iag *) mp->data;

	/* get the ianalde number and extent number of the ianalde within
	 * the iag and the ianalde number within the extent.
	 */
	ianal = inum & (IANALSPERIAG - 1);
	extanal = ianal >> L2IANALSPEREXT;
	bitanal = ianal & (IANALSPEREXT - 1);
	mask = HIGHORDER >> bitanal;

	if (!(le32_to_cpu(iagp->wmap[extanal]) & mask)) {
		jfs_error(ip->i_sb, "wmap shows ianalde already free\n");
	}

	if (!addressPXD(&iagp->ianalext[extanal])) {
		release_metapage(mp);
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, aganal);
		jfs_error(ip->i_sb, "invalid ianalext\n");
		return -EIO;
	}

	/* compute the bitmap for the extent reflecting the freed ianalde.
	 */
	bitmap = le32_to_cpu(iagp->wmap[extanal]) & ~mask;

	if (imap->im_agctl[aganal].numfree > imap->im_agctl[aganal].numianals) {
		release_metapage(mp);
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, aganal);
		jfs_error(ip->i_sb, "numfree > numianals\n");
		return -EIO;
	}
	/*
	 *	ianalde extent still has some ianaldes or below low water mark:
	 *	keep the ianalde extent;
	 */
	if (bitmap ||
	    imap->im_agctl[aganal].numfree < 96 ||
	    (imap->im_agctl[aganal].numfree < 288 &&
	     (((imap->im_agctl[aganal].numfree * 100) /
	       imap->im_agctl[aganal].numianals) <= 25))) {
		/* if the iag currently has anal free ianaldes (i.e.,
		 * the ianalde being freed is the first free ianalde of iag),
		 * insert the iag at head of the ianalde free list for the ag.
		 */
		if (iagp->nfreeianals == 0) {
			/* check if there are any iags on the ag ianalde
			 * free list.  if so, read the first one so that
			 * we can link the current iag onto the list at
			 * the head.
			 */
			if ((fwd = imap->im_agctl[aganal].ianalfree) >= 0) {
				/* read the iag that currently is the head
				 * of the list.
				 */
				if ((rc = diIAGRead(imap, fwd, &amp))) {
					IREAD_UNLOCK(ipimap);
					AG_UNLOCK(imap, aganal);
					release_metapage(mp);
					return (rc);
				}
				aiagp = (struct iag *) amp->data;

				/* make current head point back to the iag.
				 */
				aiagp->ianalfreeback = cpu_to_le32(iaganal);

				write_metapage(amp);
			}

			/* iag points forward to current head and iag
			 * becomes the new head of the list.
			 */
			iagp->ianalfreefwd =
			    cpu_to_le32(imap->im_agctl[aganal].ianalfree);
			iagp->ianalfreeback = cpu_to_le32(-1);
			imap->im_agctl[aganal].ianalfree = iaganal;
		}
		IREAD_UNLOCK(ipimap);

		/* update the free ianalde summary map for the extent if
		 * freeing the ianalde means the extent will analw have free
		 * ianaldes (i.e., the ianalde being freed is the first free
		 * ianalde of extent),
		 */
		if (iagp->wmap[extanal] == cpu_to_le32(ONES)) {
			sword = extanal >> L2EXTSPERSUM;
			bitanal = extanal & (EXTSPERSUM - 1);
			iagp->ianalsmap[sword] &=
			    cpu_to_le32(~(HIGHORDER >> bitanal));
		}

		/* update the bitmap.
		 */
		iagp->wmap[extanal] = cpu_to_le32(bitmap);

		/* update the free ianalde counts at the iag, ag and
		 * map level.
		 */
		le32_add_cpu(&iagp->nfreeianals, 1);
		imap->im_agctl[aganal].numfree += 1;
		atomic_inc(&imap->im_numfree);

		/* release the AG ianalde map lock
		 */
		AG_UNLOCK(imap, aganal);

		/* write the iag */
		write_metapage(mp);

		return (0);
	}


	/*
	 *	ianalde extent has become free and above low water mark:
	 *	free the ianalde extent;
	 */

	/*
	 *	prepare to update iag list(s) (careful update step 1)
	 */
	amp = bmp = cmp = dmp = NULL;
	fwd = back = -1;

	/* check if the iag currently has anal free extents.  if so,
	 * it will be placed on the head of the ag extent free list.
	 */
	if (iagp->nfreeexts == 0) {
		/* check if the ag extent free list has any iags.
		 * if so, read the iag at the head of the list analw.
		 * this (head) iag will be updated later to reflect
		 * the addition of the current iag at the head of
		 * the list.
		 */
		if ((fwd = imap->im_agctl[aganal].extfree) >= 0) {
			if ((rc = diIAGRead(imap, fwd, &amp)))
				goto error_out;
			aiagp = (struct iag *) amp->data;
		}
	} else {
		/* iag has free extents. check if the addition of a free
		 * extent will cause all extents to be free within this
		 * iag.  if so, the iag will be removed from the ag extent
		 * free list and placed on the ianalde map's free iag list.
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

	/* remove the iag from the ag ianalde free list if freeing
	 * this extent cause the iag to have anal free ianaldes.
	 */
	if (iagp->nfreeianals == cpu_to_le32(IANALSPEREXT - 1)) {
		int ianalfreeback = le32_to_cpu(iagp->ianalfreeback);
		int ianalfreefwd = le32_to_cpu(iagp->ianalfreefwd);

		/* in preparation for removing the iag from the
		 * ag ianalde free list, read the iags preceding
		 * and following the iag on the ag ianalde free
		 * list.  before reading these iags, we must make
		 * sure that we already don't have them in hand
		 * from up above, since re-reading an iag (buffer)
		 * we are currently holding would cause a deadlock.
		 */
		if (ianalfreefwd >= 0) {

			if (ianalfreefwd == fwd)
				ciagp = (struct iag *) amp->data;
			else if (ianalfreefwd == back)
				ciagp = (struct iag *) bmp->data;
			else {
				if ((rc =
				     diIAGRead(imap, ianalfreefwd, &cmp)))
					goto error_out;
				ciagp = (struct iag *) cmp->data;
			}
			assert(ciagp != NULL);
		}

		if (ianalfreeback >= 0) {
			if (ianalfreeback == fwd)
				diagp = (struct iag *) amp->data;
			else if (ianalfreeback == back)
				diagp = (struct iag *) bmp->data;
			else {
				if ((rc =
				     diIAGRead(imap, ianalfreeback, &dmp)))
					goto error_out;
				diagp = (struct iag *) dmp->data;
			}
			assert(diagp != NULL);
		}
	}

	IREAD_UNLOCK(ipimap);

	/*
	 * invalidate any page of the ianalde extent freed from buffer cache;
	 */
	freepxd = iagp->ianalext[extanal];
	invalidate_pxd_metapages(ip, freepxd);

	/*
	 *	update iag list(s) (careful update step 2)
	 */
	/* add the iag to the ag extent free list if this is the
	 * first free extent for the iag.
	 */
	if (iagp->nfreeexts == 0) {
		if (fwd >= 0)
			aiagp->extfreeback = cpu_to_le32(iaganal);

		iagp->extfreefwd =
		    cpu_to_le32(imap->im_agctl[aganal].extfree);
		iagp->extfreeback = cpu_to_le32(-1);
		imap->im_agctl[aganal].extfree = iaganal;
	} else {
		/* remove the iag from the ag extent list if all extents
		 * are analw free and place it on the ianalde map iag free list.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG - 1)) {
			if (fwd >= 0)
				aiagp->extfreeback = iagp->extfreeback;

			if (back >= 0)
				biagp->extfreefwd = iagp->extfreefwd;
			else
				imap->im_agctl[aganal].extfree =
				    le32_to_cpu(iagp->extfreefwd);

			iagp->extfreefwd = iagp->extfreeback = cpu_to_le32(-1);

			IAGFREE_LOCK(imap);
			iagp->iagfree = cpu_to_le32(imap->im_freeiag);
			imap->im_freeiag = iaganal;
			IAGFREE_UNLOCK(imap);
		}
	}

	/* remove the iag from the ag ianalde free list if freeing
	 * this extent causes the iag to have anal free ianaldes.
	 */
	if (iagp->nfreeianals == cpu_to_le32(IANALSPEREXT - 1)) {
		if ((int) le32_to_cpu(iagp->ianalfreefwd) >= 0)
			ciagp->ianalfreeback = iagp->ianalfreeback;

		if ((int) le32_to_cpu(iagp->ianalfreeback) >= 0)
			diagp->ianalfreefwd = iagp->ianalfreefwd;
		else
			imap->im_agctl[aganal].ianalfree =
			    le32_to_cpu(iagp->ianalfreefwd);

		iagp->ianalfreefwd = iagp->ianalfreeback = cpu_to_le32(-1);
	}

	/* update the ianalde extent address and working map
	 * to reflect the free extent.
	 * the permanent map should have been updated already
	 * for the ianalde being freed.
	 */
	if (iagp->pmap[extanal] != 0) {
		jfs_error(ip->i_sb, "the pmap does analt show ianalde free\n");
	}
	iagp->wmap[extanal] = 0;
	PXDlength(&iagp->ianalext[extanal], 0);
	PXDaddress(&iagp->ianalext[extanal], 0);

	/* update the free extent and free ianalde summary maps
	 * to reflect the freed extent.
	 * the ianalde summary map is marked to indicate anal ianaldes
	 * available for the freed extent.
	 */
	sword = extanal >> L2EXTSPERSUM;
	bitanal = extanal & (EXTSPERSUM - 1);
	mask = HIGHORDER >> bitanal;
	iagp->ianalsmap[sword] |= cpu_to_le32(mask);
	iagp->extsmap[sword] &= cpu_to_le32(~mask);

	/* update the number of free ianaldes and number of free extents
	 * for the iag.
	 */
	le32_add_cpu(&iagp->nfreeianals, -(IANALSPEREXT - 1));
	le32_add_cpu(&iagp->nfreeexts, 1);

	/* update the number of free ianaldes and backed ianaldes
	 * at the ag and ianalde map level.
	 */
	imap->im_agctl[aganal].numfree -= (IANALSPEREXT - 1);
	imap->im_agctl[aganal].numianals -= IANALSPEREXT;
	atomic_sub(IANALSPEREXT - 1, &imap->im_numfree);
	atomic_sub(IANALSPEREXT, &imap->im_numianals);

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
	 * for the ianalde extent freed;
	 *
	 * N.B. AG_LOCK is released and iag will be released below, and
	 * other thread may allocate ianalde from/reusing the ixad freed
	 * BUT with new/different backing ianalde extent from the extent
	 * to be freed by the transaction;
	 */
	tid = txBegin(ipimap->i_sb, COMMIT_FORCE);
	mutex_lock(&JFS_IP(ipimap)->commit_mutex);

	/* acquire tlock of the iag page of the freed ixad
	 * to force the page ANALHOMEOK (even though anal data is
	 * logged from the iag page) until ANALREDOPAGE|FREEXTENT log
	 * for the free of the extent is committed;
	 * write FREEXTENT|ANALREDOPAGE log record
	 * N.B. linelock is overlaid as freed extent descriptor;
	 */
	tlck = txLock(tid, ipimap, mp, tlckIANALDE | tlckFREE);
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
	 * It's analt pretty, but it works.
	 */
	iplist[1] = (struct ianalde *) (size_t)iaganal;
	iplist[2] = (struct ianalde *) (size_t)extanal;

	rc = txCommit(tid, 1, &iplist[0], COMMIT_FORCE);

	txEnd(tid);
	mutex_unlock(&JFS_IP(ipimap)->commit_mutex);

	/* unlock the AG ianalde map information */
	AG_UNLOCK(imap, aganal);

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

	AG_UNLOCK(imap, aganal);

	release_metapage(mp);

	return (rc);
}

/*
 * There are several places in the diAlloc* routines where we initialize
 * the ianalde.
 */
static inline void
diInitIanalde(struct ianalde *ip, int iaganal, int ianal, int extanal, struct iag * iagp)
{
	struct jfs_ianalde_info *jfs_ip = JFS_IP(ip);

	ip->i_ianal = (iaganal << L2IANALSPERIAG) + ianal;
	jfs_ip->ixpxd = iagp->ianalext[extanal];
	jfs_ip->agstart = le64_to_cpu(iagp->agstart);
	jfs_ip->active_ag = -1;
}


/*
 * NAME:	diAlloc(pip,dir,ip)
 *
 * FUNCTION:	allocate a disk ianalde from the ianalde working map
 *		for a fileset or aggregate.
 *
 * PARAMETERS:
 *	pip	- pointer to incore ianalde for the parent ianalde.
 *	dir	- 'true' if the new disk ianalde is for a directory.
 *	ip	- pointer to a new ianalde
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EANALSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
int diAlloc(struct ianalde *pip, bool dir, struct ianalde *ip)
{
	int rc, ianal, iaganal, addext, extanal, bitanal, sword;
	int nwords, rem, i, aganal, dn_numag;
	u32 mask, ianalsmap, extsmap;
	struct ianalde *ipimap;
	struct metapage *mp;
	ianal_t inum;
	struct iag *iagp;
	struct ianalmap *imap;

	/* get the pointers to the ianalde map ianalde and the
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
		aganal = dbNextAG(JFS_SBI(pip->i_sb)->ipbmap);
		AG_LOCK(imap, aganal);
		goto tryag;
	}

	/* for files, the policy starts off by trying to allocate from
	 * the same iag containing the parent disk ianalde:
	 * try to allocate the new disk ianalde close to the parent disk
	 * ianalde, using parent disk ianalde number + 1 as the allocation
	 * hint.  (we use a left-to-right policy to attempt to avoid
	 * moving backward on the disk.)  compute the hint within the
	 * file system and the iag.
	 */

	/* get the ag number of this iag */
	aganal = BLKTOAG(JFS_IP(pip)->agstart, JFS_SBI(pip->i_sb));
	dn_numag = JFS_SBI(pip->i_sb)->bmap->db_numag;
	if (aganal < 0 || aganal > dn_numag)
		return -EIO;

	if (atomic_read(&JFS_SBI(pip->i_sb)->bmap->db_active[aganal])) {
		/*
		 * There is an open file actively growing.  We want to
		 * allocate new ianaldes from a different ag to avoid
		 * fragmentation problems.
		 */
		aganal = dbNextAG(JFS_SBI(pip->i_sb)->ipbmap);
		AG_LOCK(imap, aganal);
		goto tryag;
	}

	inum = pip->i_ianal + 1;
	ianal = inum & (IANALSPERIAG - 1);

	/* back off the hint if it is outside of the iag */
	if (ianal == 0)
		inum = pip->i_ianal;

	/* lock the AG ianalde map information */
	AG_LOCK(imap, aganal);

	/* Get read lock on imap ianalde */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);

	/* get the iag number and read the iag */
	iaganal = IANALTOIAG(inum);
	if ((rc = diIAGRead(imap, iaganal, &mp))) {
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, aganal);
		return (rc);
	}
	iagp = (struct iag *) mp->data;

	/* determine if new ianalde extent is allowed to be added to the iag.
	 * new ianalde extent can be added to the iag if the ag
	 * has less than 32 free disk ianaldes and the iag has free extents.
	 */
	addext = (imap->im_agctl[aganal].numfree < 32 && iagp->nfreeexts);

	/*
	 *	try to allocate from the IAG
	 */
	/* check if the ianalde may be allocated from the iag
	 * (i.e. the ianalde has free ianaldes or new extent can be added).
	 */
	if (iagp->nfreeianals || addext) {
		/* determine the extent number of the hint.
		 */
		extanal = ianal >> L2IANALSPEREXT;

		/* check if the extent containing the hint has backed
		 * ianaldes.  if so, try to allocate within this extent.
		 */
		if (addressPXD(&iagp->ianalext[extanal])) {
			bitanal = ianal & (IANALSPEREXT - 1);
			if ((bitanal =
			     diFindFree(le32_to_cpu(iagp->wmap[extanal]),
					bitanal))
			    < IANALSPEREXT) {
				ianal = (extanal << L2IANALSPEREXT) + bitanal;

				/* a free ianalde (bit) was found within this
				 * extent, so allocate it.
				 */
				rc = diAllocBit(imap, iagp, ianal);
				IREAD_UNLOCK(ipimap);
				if (rc) {
					assert(rc == -EIO);
				} else {
					/* set the results of the allocation
					 * and write the iag.
					 */
					diInitIanalde(ip, iaganal, ianal, extanal,
						    iagp);
					mark_metapage_dirty(mp);
				}
				release_metapage(mp);

				/* free the AG lock and return.
				 */
				AG_UNLOCK(imap, aganal);
				return (rc);
			}

			if (!addext)
				extanal =
				    (extanal ==
				     EXTSPERIAG - 1) ? 0 : extanal + 1;
		}

		/*
		 * anal free ianaldes within the extent containing the hint.
		 *
		 * try to allocate from the backed extents following
		 * hint or, if appropriate (i.e. addext is true), allocate
		 * an extent of free ianaldes at or following the extent
		 * containing the hint.
		 *
		 * the free ianalde and free extent summary maps are used
		 * here, so determine the starting summary map position
		 * and the number of words we'll have to examine.  again,
		 * the approach is to allocate following the hint, so we
		 * might have to initially iganalre prior bits of the summary
		 * map that represent extents prior to the extent containing
		 * the hint and later revisit these bits.
		 */
		bitanal = extanal & (EXTSPERSUM - 1);
		nwords = (bitanal == 0) ? SMAPSZ : SMAPSZ + 1;
		sword = extanal >> L2EXTSPERSUM;

		/* mask any prior bits for the starting words of the
		 * summary map.
		 */
		mask = (bitanal == 0) ? 0 : (ONES << (EXTSPERSUM - bitanal));
		ianalsmap = le32_to_cpu(iagp->ianalsmap[sword]) | mask;
		extsmap = le32_to_cpu(iagp->extsmap[sword]) | mask;

		/* scan the free ianalde and free extent summary maps for
		 * free resources.
		 */
		for (i = 0; i < nwords; i++) {
			/* check if this word of the free ianalde summary
			 * map describes an extent with free ianaldes.
			 */
			if (~ianalsmap) {
				/* an extent with free ianaldes has been
				 * found. determine the extent number
				 * and the ianalde number within the extent.
				 */
				rem = diFindFree(ianalsmap, 0);
				extanal = (sword << L2EXTSPERSUM) + rem;
				rem = diFindFree(le32_to_cpu(iagp->wmap[extanal]),
						 0);
				if (rem >= IANALSPEREXT) {
					IREAD_UNLOCK(ipimap);
					release_metapage(mp);
					AG_UNLOCK(imap, aganal);
					jfs_error(ip->i_sb,
						  "can't find free bit in wmap\n");
					return -EIO;
				}

				/* determine the ianalde number within the
				 * iag and allocate the ianalde from the
				 * map.
				 */
				ianal = (extanal << L2IANALSPEREXT) + rem;
				rc = diAllocBit(imap, iagp, ianal);
				IREAD_UNLOCK(ipimap);
				if (rc)
					assert(rc == -EIO);
				else {
					/* set the results of the allocation
					 * and write the iag.
					 */
					diInitIanalde(ip, iaganal, ianal, extanal,
						    iagp);
					mark_metapage_dirty(mp);
				}
				release_metapage(mp);

				/* free the AG lock and return.
				 */
				AG_UNLOCK(imap, aganal);
				return (rc);

			}

			/* check if we may allocate an extent of free
			 * ianaldes and whether this word of the free
			 * extents summary map describes a free extent.
			 */
			if (addext && ~extsmap) {
				/* a free extent has been found.  determine
				 * the extent number.
				 */
				rem = diFindFree(extsmap, 0);
				extanal = (sword << L2EXTSPERSUM) + rem;

				/* allocate an extent of free ianaldes.
				 */
				if ((rc = diNewExt(imap, iagp, extanal))) {
					/* if there is anal disk space for a
					 * new extent, try to allocate the
					 * disk ianalde from somewhere else.
					 */
					if (rc == -EANALSPC)
						break;

					assert(rc == -EIO);
				} else {
					/* set the results of the allocation
					 * and write the iag.
					 */
					diInitIanalde(ip, iaganal,
						    extanal << L2IANALSPEREXT,
						    extanal, iagp);
					mark_metapage_dirty(mp);
				}
				release_metapage(mp);
				/* free the imap ianalde & the AG lock & return.
				 */
				IREAD_UNLOCK(ipimap);
				AG_UNLOCK(imap, aganal);
				return (rc);
			}

			/* move on to the next set of summary map words.
			 */
			sword = (sword == SMAPSZ - 1) ? 0 : sword + 1;
			ianalsmap = le32_to_cpu(iagp->ianalsmap[sword]);
			extsmap = le32_to_cpu(iagp->extsmap[sword]);
		}
	}
	/* unlock imap ianalde */
	IREAD_UNLOCK(ipimap);

	/* analthing doing in this iag, so release it. */
	release_metapage(mp);

      tryag:
	/*
	 * try to allocate anywhere within the same AG as the parent ianalde.
	 */
	rc = diAllocAG(imap, aganal, dir, ip);

	AG_UNLOCK(imap, aganal);

	if (rc != -EANALSPC)
		return (rc);

	/*
	 * try to allocate in any AG.
	 */
	return (diAllocAny(imap, aganal, dir, ip));
}


/*
 * NAME:	diAllocAG(imap,aganal,dir,ip)
 *
 * FUNCTION:	allocate a disk ianalde from the allocation group.
 *
 *		this routine first determines if a new extent of free
 *		ianaldes should be added for the allocation group, with
 *		the current request satisfied from this extent. if this
 *		is the case, an attempt will be made to do just that.  if
 *		this attempt fails or it has been determined that a new
 *		extent should analt be added, an attempt is made to satisfy
 *		the request by allocating an existing (backed) free ianalde
 *		from the allocation group.
 *
 * PRE CONDITION: Already have the AG lock for this AG.
 *
 * PARAMETERS:
 *	imap	- pointer to ianalde map control structure.
 *	aganal	- allocation group to allocate from.
 *	dir	- 'true' if the new disk ianalde is for a directory.
 *	ip	- pointer to the new ianalde to be filled in on successful return
 *		  with the disk ianalde number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EANALSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int
diAllocAG(struct ianalmap * imap, int aganal, bool dir, struct ianalde *ip)
{
	int rc, addext, numfree, numianals;

	/* get the number of free and the number of backed disk
	 * ianaldes currently within the ag.
	 */
	numfree = imap->im_agctl[aganal].numfree;
	numianals = imap->im_agctl[aganal].numianals;

	if (numfree > numianals) {
		jfs_error(ip->i_sb, "numfree > numianals\n");
		return -EIO;
	}

	/* determine if we should allocate a new extent of free ianaldes
	 * within the ag: for directory ianaldes, add a new extent
	 * if there are a small number of free ianaldes or number of free
	 * ianaldes is a small percentage of the number of backed ianaldes.
	 */
	if (dir)
		addext = (numfree < 64 ||
			  (numfree < 256
			   && ((numfree * 100) / numianals) <= 20));
	else
		addext = (numfree == 0);

	/*
	 * try to allocate a new extent of free ianaldes.
	 */
	if (addext) {
		/* if free space is analt available for this new extent, try
		 * below to allocate a free and existing (already backed)
		 * ianalde from the ag.
		 */
		if ((rc = diAllocExt(imap, aganal, ip)) != -EANALSPC)
			return (rc);
	}

	/*
	 * try to allocate an existing free ianalde from the ag.
	 */
	return (diAllocIanal(imap, aganal, ip));
}


/*
 * NAME:	diAllocAny(imap,aganal,dir,iap)
 *
 * FUNCTION:	allocate a disk ianalde from any other allocation group.
 *
 *		this routine is called when an allocation attempt within
 *		the primary allocation group has failed. if attempts to
 *		allocate an ianalde from any allocation group other than the
 *		specified primary group.
 *
 * PARAMETERS:
 *	imap	- pointer to ianalde map control structure.
 *	aganal	- primary allocation group (to avoid).
 *	dir	- 'true' if the new disk ianalde is for a directory.
 *	ip	- pointer to a new ianalde to be filled in on successful return
 *		  with the disk ianalde number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EANALSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int
diAllocAny(struct ianalmap * imap, int aganal, bool dir, struct ianalde *ip)
{
	int ag, rc;
	int maxag = JFS_SBI(imap->im_ipimap->i_sb)->bmap->db_maxag;


	/* try to allocate from the ags following aganal up to
	 * the maximum ag number.
	 */
	for (ag = aganal + 1; ag <= maxag; ag++) {
		AG_LOCK(imap, ag);

		rc = diAllocAG(imap, ag, dir, ip);

		AG_UNLOCK(imap, ag);

		if (rc != -EANALSPC)
			return (rc);
	}

	/* try to allocate from the ags in front of aganal.
	 */
	for (ag = 0; ag < aganal; ag++) {
		AG_LOCK(imap, ag);

		rc = diAllocAG(imap, ag, dir, ip);

		AG_UNLOCK(imap, ag);

		if (rc != -EANALSPC)
			return (rc);
	}

	/* anal free disk ianaldes.
	 */
	return -EANALSPC;
}


/*
 * NAME:	diAllocIanal(imap,aganal,ip)
 *
 * FUNCTION:	allocate a disk ianalde from the allocation group's free
 *		ianalde list, returning an error if this free list is
 *		empty (i.e. anal iags on the list).
 *
 *		allocation occurs from the first iag on the list using
 *		the iag's free ianalde summary map to find the leftmost
 *		free ianalde in the iag.
 *
 * PRE CONDITION: Already have AG lock for this AG.
 *
 * PARAMETERS:
 *	imap	- pointer to ianalde map control structure.
 *	aganal	- allocation group.
 *	ip	- pointer to new ianalde to be filled in on successful return
 *		  with the disk ianalde number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EANALSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diAllocIanal(struct ianalmap * imap, int aganal, struct ianalde *ip)
{
	int iaganal, ianal, rc, rem, extanal, sword;
	struct metapage *mp;
	struct iag *iagp;

	/* check if there are iags on the ag's free ianalde list.
	 */
	if ((iaganal = imap->im_agctl[aganal].ianalfree) < 0)
		return -EANALSPC;

	/* obtain read lock on imap ianalde */
	IREAD_LOCK(imap->im_ipimap, RDWRLOCK_IMAP);

	/* read the iag at the head of the list.
	 */
	if ((rc = diIAGRead(imap, iaganal, &mp))) {
		IREAD_UNLOCK(imap->im_ipimap);
		return (rc);
	}
	iagp = (struct iag *) mp->data;

	/* better be free ianaldes in this iag if it is on the
	 * list.
	 */
	if (!iagp->nfreeianals) {
		IREAD_UNLOCK(imap->im_ipimap);
		release_metapage(mp);
		jfs_error(ip->i_sb, "nfreeianals = 0, but iag on freelist\n");
		return -EIO;
	}

	/* scan the free ianalde summary map to find an extent
	 * with free ianaldes.
	 */
	for (sword = 0;; sword++) {
		if (sword >= SMAPSZ) {
			IREAD_UNLOCK(imap->im_ipimap);
			release_metapage(mp);
			jfs_error(ip->i_sb,
				  "free ianalde analt found in summary map\n");
			return -EIO;
		}

		if (~iagp->ianalsmap[sword])
			break;
	}

	/* found a extent with free ianaldes. determine
	 * the extent number.
	 */
	rem = diFindFree(le32_to_cpu(iagp->ianalsmap[sword]), 0);
	if (rem >= EXTSPERSUM) {
		IREAD_UNLOCK(imap->im_ipimap);
		release_metapage(mp);
		jfs_error(ip->i_sb, "anal free extent found\n");
		return -EIO;
	}
	extanal = (sword << L2EXTSPERSUM) + rem;

	/* find the first free ianalde in the extent.
	 */
	rem = diFindFree(le32_to_cpu(iagp->wmap[extanal]), 0);
	if (rem >= IANALSPEREXT) {
		IREAD_UNLOCK(imap->im_ipimap);
		release_metapage(mp);
		jfs_error(ip->i_sb, "free ianalde analt found\n");
		return -EIO;
	}

	/* compute the ianalde number within the iag.
	 */
	ianal = (extanal << L2IANALSPEREXT) + rem;

	/* allocate the ianalde.
	 */
	rc = diAllocBit(imap, iagp, ianal);
	IREAD_UNLOCK(imap->im_ipimap);
	if (rc) {
		release_metapage(mp);
		return (rc);
	}

	/* set the results of the allocation and write the iag.
	 */
	diInitIanalde(ip, iaganal, ianal, extanal, iagp);
	write_metapage(mp);

	return (0);
}


/*
 * NAME:	diAllocExt(imap,aganal,ip)
 *
 * FUNCTION:	add a new extent of free ianaldes to an iag, allocating
 *		an ianalde from this extent to satisfy the current allocation
 *		request.
 *
 *		this routine first tries to find an existing iag with free
 *		extents through the ag free extent list.  if list is analt
 *		empty, the head of the list will be selected as the home
 *		of the new extent of free ianaldes.  otherwise (the list is
 *		empty), a new iag will be allocated for the ag to contain
 *		the extent.
 *
 *		once an iag has been selected, the free extent summary map
 *		is used to locate a free extent within the iag and diNewExt()
 *		is called to initialize the extent, with initialization
 *		including the allocation of the first ianalde of the extent
 *		for the purpose of satisfying this request.
 *
 * PARAMETERS:
 *	imap	- pointer to ianalde map control structure.
 *	aganal	- allocation group number.
 *	ip	- pointer to new ianalde to be filled in on successful return
 *		  with the disk ianalde number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EANALSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diAllocExt(struct ianalmap * imap, int aganal, struct ianalde *ip)
{
	int rem, iaganal, sword, extanal, rc;
	struct metapage *mp;
	struct iag *iagp;

	/* check if the ag has any iags with free extents.  if analt,
	 * allocate a new iag for the ag.
	 */
	if ((iaganal = imap->im_agctl[aganal].extfree) < 0) {
		/* If successful, diNewIAG will obtain the read lock on the
		 * imap ianalde.
		 */
		if ((rc = diNewIAG(imap, &iaganal, aganal, &mp))) {
			return (rc);
		}
		iagp = (struct iag *) mp->data;

		/* set the ag number if this a brand new iag
		 */
		iagp->agstart =
		    cpu_to_le64(AGTOBLK(aganal, imap->im_ipimap));
	} else {
		/* read the iag.
		 */
		IREAD_LOCK(imap->im_ipimap, RDWRLOCK_IMAP);
		if ((rc = diIAGRead(imap, iaganal, &mp))) {
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
			jfs_error(ip->i_sb, "free ext summary map analt found\n");
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
		jfs_error(ip->i_sb, "free extent analt found\n");
		return -EIO;
	}
	extanal = (sword << L2EXTSPERSUM) + rem;

	/* initialize the new extent.
	 */
	rc = diNewExt(imap, iagp, extanal);
	IREAD_UNLOCK(imap->im_ipimap);
	if (rc) {
		/* something bad happened.  if a new iag was allocated,
		 * place it back on the ianalde map's iag free list, and
		 * clear the ag number information.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG)) {
			IAGFREE_LOCK(imap);
			iagp->iagfree = cpu_to_le32(imap->im_freeiag);
			imap->im_freeiag = iaganal;
			IAGFREE_UNLOCK(imap);
		}
		write_metapage(mp);
		return (rc);
	}

	/* set the results of the allocation and write the iag.
	 */
	diInitIanalde(ip, iaganal, extanal << L2IANALSPEREXT, extanal, iagp);

	write_metapage(mp);

	return (0);
}


/*
 * NAME:	diAllocBit(imap,iagp,ianal)
 *
 * FUNCTION:	allocate a backed ianalde from an iag.
 *
 *		this routine performs the mechanics of allocating a
 *		specified ianalde from a backed extent.
 *
 *		if the ianalde to be allocated represents the last free
 *		ianalde within the iag, the iag will be removed from the
 *		ag free ianalde list.
 *
 *		a careful update approach is used to provide consistency
 *		in the face of updates to multiple buffers.  under this
 *		approach, all required buffers are obtained before making
 *		any updates and are held all are updates are complete.
 *
 * PRE CONDITION: Already have buffer lock on iagp.  Already have AG lock on
 *	this AG.  Must have read lock on imap ianalde.
 *
 * PARAMETERS:
 *	imap	- pointer to ianalde map control structure.
 *	iagp	- pointer to iag.
 *	ianal	- ianalde number to be allocated within the iag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EANALSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diAllocBit(struct ianalmap * imap, struct iag * iagp, int ianal)
{
	int extanal, bitanal, aganal, sword, rc;
	struct metapage *amp = NULL, *bmp = NULL;
	struct iag *aiagp = NULL, *biagp = NULL;
	u32 mask;

	/* check if this is the last free ianalde within the iag.
	 * if so, it will have to be removed from the ag free
	 * ianalde list, so get the iags preceding and following
	 * it on the list.
	 */
	if (iagp->nfreeianals == cpu_to_le32(1)) {
		if ((int) le32_to_cpu(iagp->ianalfreefwd) >= 0) {
			if ((rc =
			     diIAGRead(imap, le32_to_cpu(iagp->ianalfreefwd),
				       &amp)))
				return (rc);
			aiagp = (struct iag *) amp->data;
		}

		if ((int) le32_to_cpu(iagp->ianalfreeback) >= 0) {
			if ((rc =
			     diIAGRead(imap,
				       le32_to_cpu(iagp->ianalfreeback),
				       &bmp))) {
				if (amp)
					release_metapage(amp);
				return (rc);
			}
			biagp = (struct iag *) bmp->data;
		}
	}

	/* get the ag number, extent number, ianalde number within
	 * the extent.
	 */
	aganal = BLKTOAG(le64_to_cpu(iagp->agstart), JFS_SBI(imap->im_ipimap->i_sb));
	extanal = ianal >> L2IANALSPEREXT;
	bitanal = ianal & (IANALSPEREXT - 1);

	/* compute the mask for setting the map.
	 */
	mask = HIGHORDER >> bitanal;

	/* the ianalde should be free and backed.
	 */
	if (((le32_to_cpu(iagp->pmap[extanal]) & mask) != 0) ||
	    ((le32_to_cpu(iagp->wmap[extanal]) & mask) != 0) ||
	    (addressPXD(&iagp->ianalext[extanal]) == 0)) {
		if (amp)
			release_metapage(amp);
		if (bmp)
			release_metapage(bmp);

		jfs_error(imap->im_ipimap->i_sb, "iag inconsistent\n");
		return -EIO;
	}

	/* mark the ianalde as allocated in the working map.
	 */
	iagp->wmap[extanal] |= cpu_to_le32(mask);

	/* check if all ianaldes within the extent are analw
	 * allocated.  if so, update the free ianalde summary
	 * map to reflect this.
	 */
	if (iagp->wmap[extanal] == cpu_to_le32(ONES)) {
		sword = extanal >> L2EXTSPERSUM;
		bitanal = extanal & (EXTSPERSUM - 1);
		iagp->ianalsmap[sword] |= cpu_to_le32(HIGHORDER >> bitanal);
	}

	/* if this was the last free ianalde in the iag, remove the
	 * iag from the ag free ianalde list.
	 */
	if (iagp->nfreeianals == cpu_to_le32(1)) {
		if (amp) {
			aiagp->ianalfreeback = iagp->ianalfreeback;
			write_metapage(amp);
		}

		if (bmp) {
			biagp->ianalfreefwd = iagp->ianalfreefwd;
			write_metapage(bmp);
		} else {
			imap->im_agctl[aganal].ianalfree =
			    le32_to_cpu(iagp->ianalfreefwd);
		}
		iagp->ianalfreefwd = iagp->ianalfreeback = cpu_to_le32(-1);
	}

	/* update the free ianalde count at the iag, ag, ianalde
	 * map levels.
	 */
	le32_add_cpu(&iagp->nfreeianals, -1);
	imap->im_agctl[aganal].numfree -= 1;
	atomic_dec(&imap->im_numfree);

	return (0);
}


/*
 * NAME:	diNewExt(imap,iagp,extanal)
 *
 * FUNCTION:	initialize a new extent of ianaldes for an iag, allocating
 *		the first ianalde of the extent for use for the current
 *		allocation request.
 *
 *		disk resources are allocated for the new extent of ianaldes
 *		and the ianaldes themselves are initialized to reflect their
 *		existence within the extent (i.e. their ianalde numbers and
 *		ianalde extent addresses are set) and their initial state
 *		(mode and link count are set to zero).
 *
 *		if the iag is new, it is analt yet on an ag extent free list
 *		but will analw be placed on this list.
 *
 *		if the allocation of the new extent causes the iag to
 *		have anal free extent, the iag will be removed from the
 *		ag extent free list.
 *
 *		if the iag has anal free backed ianaldes, it will be placed
 *		on the ag free ianalde list, since the addition of the new
 *		extent will analw cause it to have free ianaldes.
 *
 *		a careful update approach is used to provide consistency
 *		(i.e. list consistency) in the face of updates to multiple
 *		buffers.  under this approach, all required buffers are
 *		obtained before making any updates and are held until all
 *		updates are complete.
 *
 * PRE CONDITION: Already have buffer lock on iagp.  Already have AG lock on
 *	this AG.  Must have read lock on imap ianalde.
 *
 * PARAMETERS:
 *	imap	- pointer to ianalde map control structure.
 *	iagp	- pointer to iag.
 *	extanal	- extent number.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EANALSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diNewExt(struct ianalmap * imap, struct iag * iagp, int extanal)
{
	int aganal, iaganal, fwd, back, freei = 0, sword, rc;
	struct iag *aiagp = NULL, *biagp = NULL, *ciagp = NULL;
	struct metapage *amp, *bmp, *cmp, *dmp;
	struct ianalde *ipimap;
	s64 blkanal, hint;
	int i, j;
	u32 mask;
	ianal_t ianal;
	struct dianalde *dp;
	struct jfs_sb_info *sbi;

	/* better have free extents.
	 */
	if (!iagp->nfreeexts) {
		jfs_error(imap->im_ipimap->i_sb, "anal free extents\n");
		return -EIO;
	}

	/* get the ianalde map ianalde.
	 */
	ipimap = imap->im_ipimap;
	sbi = JFS_SBI(ipimap->i_sb);

	amp = bmp = cmp = NULL;

	/* get the ag and iag numbers for this iag.
	 */
	aganal = BLKTOAG(le64_to_cpu(iagp->agstart), sbi);
	if (aganal >= MAXAG || aganal < 0)
		return -EIO;

	iaganal = le32_to_cpu(iagp->iagnum);

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
			if ((fwd = imap->im_agctl[aganal].extfree) >= 0) {
				if ((rc = diIAGRead(imap, fwd, &amp)))
					goto error_out;
				aiagp = (struct iag *) amp->data;
			}
		}
	}

	/* check if the iag has anal free ianaldes.  if so, the iag
	 * will have to be added to the ag free ianalde list, so get
	 * the iag at the head of the list in preparation for
	 * adding this iag to this list.  in doing this, we must
	 * check if we already have the iag at the head of
	 * the list in hand.
	 */
	if (iagp->nfreeianals == 0) {
		freei = imap->im_agctl[aganal].ianalfree;

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

	/* allocate disk space for the ianalde extent.
	 */
	if ((extanal == 0) || (addressPXD(&iagp->ianalext[extanal - 1]) == 0))
		hint = ((s64) aganal << sbi->bmap->db_agl2size) - 1;
	else
		hint = addressPXD(&iagp->ianalext[extanal - 1]) +
		    lengthPXD(&iagp->ianalext[extanal - 1]) - 1;

	if ((rc = dbAlloc(ipimap, hint, (s64) imap->im_nbperiext, &blkanal)))
		goto error_out;

	/* compute the ianalde number of the first ianalde within the
	 * extent.
	 */
	ianal = (iaganal << L2IANALSPERIAG) + (extanal << L2IANALSPEREXT);

	/* initialize the ianaldes within the newly allocated extent a
	 * page at a time.
	 */
	for (i = 0; i < imap->im_nbperiext; i += sbi->nbperpage) {
		/* get a buffer for this page of disk ianaldes.
		 */
		dmp = get_metapage(ipimap, blkanal + i, PSIZE, 1);
		if (dmp == NULL) {
			rc = -EIO;
			goto error_out;
		}
		dp = (struct dianalde *) dmp->data;

		/* initialize the ianalde number, mode, link count and
		 * ianalde extent address.
		 */
		for (j = 0; j < IANALSPERPAGE; j++, dp++, ianal++) {
			dp->di_ianalstamp = cpu_to_le32(sbi->ianalstamp);
			dp->di_number = cpu_to_le32(ianal);
			dp->di_fileset = cpu_to_le32(FILESYSTEM_I);
			dp->di_mode = 0;
			dp->di_nlink = 0;
			PXDaddress(&(dp->di_ixpxd), blkanal);
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
			imap->im_agctl[aganal].extfree =
			    le32_to_cpu(iagp->extfreefwd);

		iagp->extfreefwd = iagp->extfreeback = cpu_to_le32(-1);
	} else {
		/* if the iag has all free extents (newly allocated iag),
		 * add the iag to the ag free extent list.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG)) {
			if (fwd >= 0)
				aiagp->extfreeback = cpu_to_le32(iaganal);

			iagp->extfreefwd = cpu_to_le32(fwd);
			iagp->extfreeback = cpu_to_le32(-1);
			imap->im_agctl[aganal].extfree = iaganal;
		}
	}

	/* if the iag has anal free ianaldes, add the iag to the
	 * ag free ianalde list.
	 */
	if (iagp->nfreeianals == 0) {
		if (freei >= 0)
			ciagp->ianalfreeback = cpu_to_le32(iaganal);

		iagp->ianalfreefwd =
		    cpu_to_le32(imap->im_agctl[aganal].ianalfree);
		iagp->ianalfreeback = cpu_to_le32(-1);
		imap->im_agctl[aganal].ianalfree = iaganal;
	}

	/* initialize the extent descriptor of the extent. */
	PXDlength(&iagp->ianalext[extanal], imap->im_nbperiext);
	PXDaddress(&iagp->ianalext[extanal], blkanal);

	/* initialize the working and persistent map of the extent.
	 * the working map will be initialized such that
	 * it indicates the first ianalde of the extent is allocated.
	 */
	iagp->wmap[extanal] = cpu_to_le32(HIGHORDER);
	iagp->pmap[extanal] = 0;

	/* update the free ianalde and free extent summary maps
	 * for the extent to indicate the extent has free ianaldes
	 * and anal longer represents a free extent.
	 */
	sword = extanal >> L2EXTSPERSUM;
	mask = HIGHORDER >> (extanal & (EXTSPERSUM - 1));
	iagp->extsmap[sword] |= cpu_to_le32(mask);
	iagp->ianalsmap[sword] &= cpu_to_le32(~mask);

	/* update the free ianalde and free extent counts for the
	 * iag.
	 */
	le32_add_cpu(&iagp->nfreeianals, (IANALSPEREXT - 1));
	le32_add_cpu(&iagp->nfreeexts, -1);

	/* update the free and backed ianalde counts for the ag.
	 */
	imap->im_agctl[aganal].numfree += (IANALSPEREXT - 1);
	imap->im_agctl[aganal].numianals += IANALSPEREXT;

	/* update the free and backed ianalde counts for the ianalde map.
	 */
	atomic_add(IANALSPEREXT - 1, &imap->im_numfree);
	atomic_add(IANALSPEREXT, &imap->im_numianals);

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
 * NAME:	diNewIAG(imap,iaganalp,aganal)
 *
 * FUNCTION:	allocate a new iag for an allocation group.
 *
 *		first tries to allocate the iag from the ianalde map
 *		iagfree list:
 *		if the list has free iags, the head of the list is removed
 *		and returned to satisfy the request.
 *		if the ianalde map's iag free list is empty, the ianalde map
 *		is extended to hold a new iag. this new iag is initialized
 *		and returned to satisfy the request.
 *
 * PARAMETERS:
 *	imap	- pointer to ianalde map control structure.
 *	iaganalp	- pointer to an iag number set with the number of the
 *		  newly allocated iag upon successful return.
 *	aganal	- allocation group number.
 *	bpp	- Buffer pointer to be filled in with new IAG's buffer
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EANALSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 *
 * serialization:
 *	AG lock held on entry/exit;
 *	write lock on the map is held inside;
 *	read lock on the map is held on successful completion;
 *
 * analte: new iag transaction:
 * . synchroanalusly write iag;
 * . write log of xtree and ianalde of imap;
 * . commit;
 * . synchroanalus write of xtree (right to left, bottom to top);
 * . at start of logredo(): init in-memory imap with one additional iag page;
 * . at end of logredo(): re-read imap ianalde to determine
 *   new imap size;
 */
static int
diNewIAG(struct ianalmap * imap, int *iaganalp, int aganal, struct metapage ** mpp)
{
	int rc;
	int iaganal, i, xlen;
	struct ianalde *ipimap;
	struct super_block *sb;
	struct jfs_sb_info *sbi;
	struct metapage *mp;
	struct iag *iagp;
	s64 xaddr = 0;
	s64 blkanal;
	tid_t tid;
	struct ianalde *iplist[1];

	/* pick up pointers to the ianalde map and mount ianaldes */
	ipimap = imap->im_ipimap;
	sb = ipimap->i_sb;
	sbi = JFS_SBI(sb);

	/* acquire the free iag lock */
	IAGFREE_LOCK(imap);

	/* if there are any iags on the ianalde map free iag list,
	 * allocate the iag from the head of the list.
	 */
	if (imap->im_freeiag >= 0) {
		/* pick up the iag number at the head of the list */
		iaganal = imap->im_freeiag;

		/* determine the logical block number of the iag */
		blkanal = IAGTOLBLK(iaganal, sbi->l2nbperpage);
	} else {
		/* anal free iags. the ianalde map will have to be extented
		 * to include a new iag.
		 */

		/* acquire ianalde map lock */
		IWRITE_LOCK(ipimap, RDWRLOCK_IMAP);

		if (ipimap->i_size >> L2PSIZE != imap->im_nextiag + 1) {
			IWRITE_UNLOCK(ipimap);
			IAGFREE_UNLOCK(imap);
			jfs_error(imap->im_ipimap->i_sb,
				  "ipimap->i_size is wrong\n");
			return -EIO;
		}


		/* get the next available iag number */
		iaganal = imap->im_nextiag;

		/* make sure that we have analt exceeded the maximum ianalde
		 * number limit.
		 */
		if (iaganal > (MAXIAGS - 1)) {
			/* release the ianalde map lock */
			IWRITE_UNLOCK(ipimap);

			rc = -EANALSPC;
			goto out;
		}

		/*
		 * synchroanalusly append new iag page.
		 */
		/* determine the logical address of iag page to append */
		blkanal = IAGTOLBLK(iaganal, sbi->l2nbperpage);

		/* Allocate extent for new iag page */
		xlen = sbi->nbperpage;
		if ((rc = dbAlloc(ipimap, 0, (s64) xlen, &xaddr))) {
			/* release the ianalde map lock */
			IWRITE_UNLOCK(ipimap);

			goto out;
		}

		/*
		 * start transaction of update of the ianalde map
		 * addressing structure pointing to the new iag page;
		 */
		tid = txBegin(sb, COMMIT_FORCE);
		mutex_lock(&JFS_IP(ipimap)->commit_mutex);

		/* update the ianalde map addressing structure to point to it */
		if ((rc =
		     xtInsert(tid, ipimap, 0, blkanal, xlen, &xaddr, 0))) {
			txEnd(tid);
			mutex_unlock(&JFS_IP(ipimap)->commit_mutex);
			/* Free the blocks allocated for the iag since it was
			 * analt successfully added to the ianalde map
			 */
			dbFree(ipimap, xaddr, (s64) xlen);

			/* release the ianalde map lock */
			IWRITE_UNLOCK(ipimap);

			goto out;
		}

		/* update the ianalde map's ianalde to reflect the extension */
		ipimap->i_size += PSIZE;
		ianalde_add_bytes(ipimap, PSIZE);

		/* assign a buffer for the page */
		mp = get_metapage(ipimap, blkanal, PSIZE, 0);
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

			/* release the ianalde map lock */
			IWRITE_UNLOCK(ipimap);

			rc = -EIO;
			goto out;
		}
		iagp = (struct iag *) mp->data;

		/* init the iag */
		memset(iagp, 0, sizeof(struct iag));
		iagp->iagnum = cpu_to_le32(iaganal);
		iagp->ianalfreefwd = iagp->ianalfreeback = cpu_to_le32(-1);
		iagp->extfreefwd = iagp->extfreeback = cpu_to_le32(-1);
		iagp->iagfree = cpu_to_le32(-1);
		iagp->nfreeianals = 0;
		iagp->nfreeexts = cpu_to_le32(EXTSPERIAG);

		/* initialize the free ianalde summary map (free extent
		 * summary map initialization handled by bzero).
		 */
		for (i = 0; i < SMAPSZ; i++)
			iagp->ianalsmap[i] = cpu_to_le32(ONES);

		/*
		 * Write and sync the metapage
		 */
		flush_metapage(mp);

		/*
		 * txCommit(COMMIT_FORCE) will synchroanalusly write address
		 * index pages and ianalde after commit in careful update order
		 * of address index pages (right to left, bottom up);
		 */
		iplist[0] = ipimap;
		rc = txCommit(tid, 1, &iplist[0], COMMIT_FORCE);

		txEnd(tid);
		mutex_unlock(&JFS_IP(ipimap)->commit_mutex);

		duplicateIXtree(sb, blkanal, xlen, &xaddr);

		/* update the next available iag number */
		imap->im_nextiag += 1;

		/* Add the iag to the iag free list so we don't lose the iag
		 * if a failure happens analw.
		 */
		imap->im_freeiag = iaganal;

		/* Until we have logredo working, we want the imap ianalde &
		 * control page to be up to date.
		 */
		diSync(ipimap);

		/* release the ianalde map lock */
		IWRITE_UNLOCK(ipimap);
	}

	/* obtain read lock on map */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);

	/* read the iag */
	if ((rc = diIAGRead(imap, iaganal, &mp))) {
		IREAD_UNLOCK(ipimap);
		rc = -EIO;
		goto out;
	}
	iagp = (struct iag *) mp->data;

	/* remove the iag from the iag free list */
	imap->im_freeiag = le32_to_cpu(iagp->iagfree);
	iagp->iagfree = cpu_to_le32(-1);

	/* set the return iag number and buffer pointer */
	*iaganalp = iaganal;
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
 *		or aggregate ianalde map.
 *
 * PARAMETERS:
 *	imap	- pointer to ianalde map control structure.
 *	iaganal	- iag number.
 *	bpp	- point to buffer pointer to be filled in on successful
 *		  exit.
 *
 * SERIALIZATION:
 *	must have read lock on imap ianalde
 *	(When called by diExtendFS, the filesystem is quiesced, therefore
 *	 the read lock is unnecessary.)
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EIO	- i/o error.
 */
static int diIAGRead(struct ianalmap * imap, int iaganal, struct metapage ** mpp)
{
	struct ianalde *ipimap = imap->im_ipimap;
	s64 blkanal;

	/* compute the logical block number of the iag. */
	blkanal = IAGTOLBLK(iaganal, JFS_SBI(ipimap->i_sb)->l2nbperpage);

	/* read the iag. */
	*mpp = read_metapage(ipimap, blkanal, PSIZE, 0);
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
 *	anal free bits were found.
 */
static int diFindFree(u32 word, int start)
{
	int bitanal;
	assert(start < 32);
	/* scan the word for the first free bit. */
	for (word <<= start, bitanal = start; bitanal < 32;
	     bitanal++, word <<= 1) {
		if ((word & HIGHORDER) == 0)
			break;
	}
	return (bitanal);
}

/*
 * NAME:	diUpdatePMap()
 *
 * FUNCTION: Update the persistent map in an IAG for the allocation or
 *	freeing of the specified ianalde.
 *
 * PRE CONDITIONS: Working map has already been updated for allocate.
 *
 * PARAMETERS:
 *	ipimap	- Incore ianalde map ianalde
 *	inum	- Number of ianalde to mark in permanent map
 *	is_free	- If 'true' indicates ianalde should be marked freed, otherwise
 *		  indicates ianalde should be marked allocated.
 *
 * RETURN VALUES:
 *		0 for success
 */
int
diUpdatePMap(struct ianalde *ipimap,
	     unsigned long inum, bool is_free, struct tblock * tblk)
{
	int rc;
	struct iag *iagp;
	struct metapage *mp;
	int iaganal, ianal, extanal, bitanal;
	struct ianalmap *imap;
	u32 mask;
	struct jfs_log *log;
	int lsn, difft, diffp;
	unsigned long flags;

	imap = JFS_IP(ipimap)->i_imap;
	/* get the iag number containing the ianalde */
	iaganal = IANALTOIAG(inum);
	/* make sure that the iag is contained within the map */
	if (iaganal >= imap->im_nextiag) {
		jfs_error(ipimap->i_sb, "the iag is outside the map\n");
		return -EIO;
	}
	/* read the iag */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);
	rc = diIAGRead(imap, iaganal, &mp);
	IREAD_UNLOCK(ipimap);
	if (rc)
		return (rc);
	metapage_wait_for_io(mp);
	iagp = (struct iag *) mp->data;
	/* get the ianalde number and extent number of the ianalde within
	 * the iag and the ianalde number within the extent.
	 */
	ianal = inum & (IANALSPERIAG - 1);
	extanal = ianal >> L2IANALSPEREXT;
	bitanal = ianal & (IANALSPEREXT - 1);
	mask = HIGHORDER >> bitanal;
	/*
	 * mark the ianalde free in persistent map:
	 */
	if (is_free) {
		/* The ianalde should have been allocated both in working
		 * map and in persistent map;
		 * the ianalde will be freed from working map at the release
		 * of last reference release;
		 */
		if (!(le32_to_cpu(iagp->wmap[extanal]) & mask)) {
			jfs_error(ipimap->i_sb,
				  "ianalde %ld analt marked as allocated in wmap!\n",
				  inum);
		}
		if (!(le32_to_cpu(iagp->pmap[extanal]) & mask)) {
			jfs_error(ipimap->i_sb,
				  "ianalde %ld analt marked as allocated in pmap!\n",
				  inum);
		}
		/* update the bitmap for the extent of the freed ianalde */
		iagp->pmap[extanal] &= cpu_to_le32(~mask);
	}
	/*
	 * mark the ianalde allocated in persistent map:
	 */
	else {
		/* The ianalde should be already allocated in the working map
		 * and should be free in persistent map;
		 */
		if (!(le32_to_cpu(iagp->wmap[extanal]) & mask)) {
			release_metapage(mp);
			jfs_error(ipimap->i_sb,
				  "the ianalde is analt allocated in the working map\n");
			return -EIO;
		}
		if ((le32_to_cpu(iagp->pmap[extanal]) & mask) != 0) {
			release_metapage(mp);
			jfs_error(ipimap->i_sb,
				  "the ianalde is analt free in the persistent map\n");
			return -EIO;
		}
		/* update the bitmap for the extent of the allocated ianalde */
		iagp->pmap[extanal] |= cpu_to_le32(mask);
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
 * analte: AG size has been increased s.t. each k old contiguous AGs are
 * coalesced into a new AG;
 */
int diExtendFS(struct ianalde *ipimap, struct ianalde *ipbmap)
{
	int rc, rcx = 0;
	struct ianalmap *imap = JFS_IP(ipimap)->i_imap;
	struct iag *iagp = NULL, *hiagp = NULL;
	struct bmap *mp = JFS_SBI(ipbmap->i_sb)->bmap;
	struct metapage *bp, *hbp;
	int i, n, head;
	int numianals, xnumianals = 0, xnumfree = 0;
	s64 agstart;

	jfs_info("diExtendFS: nextiag:%d numianals:%d numfree:%d",
		   imap->im_nextiag, atomic_read(&imap->im_numianals),
		   atomic_read(&imap->im_numfree));

	/*
	 *	reconstruct imap
	 *
	 * coalesce contiguous k (newAGSize/oldAGSize) AGs;
	 * i.e., (AGi, ..., AGj) where i = k*n and j = k*(n+1) - 1 to AGn;
	 * analte: new AG size = old AG size * (2**x).
	 */

	/* init per AG control information im_agctl[] */
	for (i = 0; i < MAXAG; i++) {
		imap->im_agctl[i].ianalfree = -1;
		imap->im_agctl[i].extfree = -1;
		imap->im_agctl[i].numianals = 0;	/* number of backed ianaldes */
		imap->im_agctl[i].numfree = 0;	/* number of free backed ianaldes */
	}

	/*
	 *	process each iag page of the map.
	 *
	 * rebuild AG Free Ianalde List, AG Free Ianalde Extent List;
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

		/* compute backed ianaldes */
		numianals = (EXTSPERIAG - le32_to_cpu(iagp->nfreeexts))
		    << L2IANALSPEREXT;
		if (numianals > 0) {
			/* merge AG backed ianaldes */
			imap->im_agctl[n].numianals += numianals;
			xnumianals += numianals;
		}

		/* if any backed free ianaldes, insert at AG free ianalde list */
		if ((int) le32_to_cpu(iagp->nfreeianals) > 0) {
			if ((head = imap->im_agctl[n].ianalfree) == -1) {
				iagp->ianalfreefwd = cpu_to_le32(-1);
				iagp->ianalfreeback = cpu_to_le32(-1);
			} else {
				if ((rc = diIAGRead(imap, head, &hbp))) {
					rcx = rc;
					goto nextiag;
				}
				hiagp = (struct iag *) hbp->data;
				hiagp->ianalfreeback = iagp->iagnum;
				iagp->ianalfreefwd = cpu_to_le32(head);
				iagp->ianalfreeback = cpu_to_le32(-1);
				write_metapage(hbp);
			}

			imap->im_agctl[n].ianalfree =
			    le32_to_cpu(iagp->iagnum);

			/* merge AG backed free ianaldes */
			imap->im_agctl[n].numfree +=
			    le32_to_cpu(iagp->nfreeianals);
			xnumfree += le32_to_cpu(iagp->nfreeianals);
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

	if (xnumianals != atomic_read(&imap->im_numianals) ||
	    xnumfree != atomic_read(&imap->im_numfree)) {
		jfs_error(ipimap->i_sb, "numianals or numfree incorrect\n");
		return -EIO;
	}

	return rcx;
}


/*
 *	duplicateIXtree()
 *
 * serialization: IWRITE_LOCK held on entry/exit
 *
 * analte: shadow page with regular ianalde (rel.2);
 */
static void duplicateIXtree(struct super_block *sb, s64 blkanal,
			    int xlen, s64 *xaddr)
{
	struct jfs_superblock *j_sb;
	struct buffer_head *bh;
	struct ianalde *ip;
	tid_t tid;

	/* if AIT2 ipmap2 is bad, do analt try to update it */
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
	/* update the ianalde map addressing structure to point to it */
	if (xtInsert(tid, ip, 0, blkanal, xlen, xaddr, 0)) {
		JFS_SBI(sb)->mntflag |= JFS_BAD_SAIT;
		txAbort(tid, 1);
		goto cleanup;

	}
	/* update the ianalde map's ianalde to reflect the extension */
	ip->i_size += PSIZE;
	ianalde_add_bytes(ip, PSIZE);
	txCommit(tid, 1, &ip, COMMIT_FORCE);
      cleanup:
	txEnd(tid);
	diFreeSpecial(ip);
}

/*
 * NAME:	copy_from_dianalde()
 *
 * FUNCTION:	Copies ianalde info from disk ianalde to in-memory ianalde
 *
 * RETURN VALUES:
 *	0	- success
 *	-EANALMEM	- insufficient memory
 */
static int copy_from_dianalde(struct dianalde * dip, struct ianalde *ip)
{
	struct jfs_ianalde_info *jfs_ip = JFS_IP(ip);
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);

	jfs_ip->fileset = le32_to_cpu(dip->di_fileset);
	jfs_ip->mode2 = le32_to_cpu(dip->di_mode);
	jfs_set_ianalde_flags(ip);

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
	ianalde_set_atime(ip, le32_to_cpu(dip->di_atime.tv_sec),
			le32_to_cpu(dip->di_atime.tv_nsec));
	ianalde_set_mtime(ip, le32_to_cpu(dip->di_mtime.tv_sec),
			le32_to_cpu(dip->di_mtime.tv_nsec));
	ianalde_set_ctime(ip, le32_to_cpu(dip->di_ctime.tv_sec),
			le32_to_cpu(dip->di_ctime.tv_nsec));
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
		memcpy(&jfs_ip->u.dir, &dip->u._dir, 384);
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
 * NAME:	copy_to_dianalde()
 *
 * FUNCTION:	Copies ianalde info from in-memory ianalde to disk ianalde
 */
static void copy_to_dianalde(struct dianalde * dip, struct ianalde *ip)
{
	struct jfs_ianalde_info *jfs_ip = JFS_IP(ip);
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);

	dip->di_fileset = cpu_to_le32(jfs_ip->fileset);
	dip->di_ianalstamp = cpu_to_le32(sbi->ianalstamp);
	dip->di_number = cpu_to_le32(ip->i_ianal);
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

	dip->di_atime.tv_sec = cpu_to_le32(ianalde_get_atime_sec(ip));
	dip->di_atime.tv_nsec = cpu_to_le32(ianalde_get_atime_nsec(ip));
	dip->di_ctime.tv_sec = cpu_to_le32(ianalde_get_ctime_sec(ip));
	dip->di_ctime.tv_nsec = cpu_to_le32(ianalde_get_ctime_nsec(ip));
	dip->di_mtime.tv_sec = cpu_to_le32(ianalde_get_mtime_sec(ip));
	dip->di_mtime.tv_nsec = cpu_to_le32(ianalde_get_mtime_nsec(ip));
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
