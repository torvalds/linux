/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 *	jfs_imap.c: inode allocation map manager
 *
 * Serialization:
 *   Each AG has a simple lock which is used to control the serialization of
 *	the AG level lists.  This lock should be taken first whenever an AG
 *	level list will be modified or accessed.
 *
 *   Each IAG is locked by obtaining the buffer for the IAG page.
 *
 *   There is also a inode lock for the inode map inode.  A read lock needs to
 *	be taken whenever an IAG is read from the map or the global level
 *	information is read.  A write lock needs to be taken whenever the global
 *	level information is modified or an atomic operation needs to be used.
 *
 *	If more than one IAG is read at one time, the read lock may not
 *	be given up until all of the IAG's are read.  Otherwise, a deadlock
 *	may occur when trying to obtain the read lock while another thread
 *	holding the read lock is waiting on the IAG already being held.
 *
 *   The control page of the inode map is read into memory by diMount().
 *	Thereafter it should only be modified in memory and then it will be
 *	written out when the filesystem is unmounted by diUnmount().
 */

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/slab.h>

#include "jfs_incore.h"
#include "jfs_inode.h"
#include "jfs_filsys.h"
#include "jfs_dinode.h"
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
#define AG_LOCK(imap,agno)		mutex_lock(&imap->im_aglock[agno])
#define AG_UNLOCK(imap,agno)		mutex_unlock(&imap->im_aglock[agno])

/*
 * forward references
 */
static int diAllocAG(struct inomap *, int, bool, struct inode *);
static int diAllocAny(struct inomap *, int, bool, struct inode *);
static int diAllocBit(struct inomap *, struct iag *, int);
static int diAllocExt(struct inomap *, int, struct inode *);
static int diAllocIno(struct inomap *, int, struct inode *);
static int diFindFree(u32, int);
static int diNewExt(struct inomap *, struct iag *, int);
static int diNewIAG(struct inomap *, int *, int, struct metapage **);
static void duplicateIXtree(struct super_block *, s64, int, s64 *);

static int diIAGRead(struct inomap * imap, int, struct metapage **);
static int copy_from_dinode(struct dinode *, struct inode *);
static void copy_to_dinode(struct dinode *, struct inode *);

/*
 * NAME:	diMount()
 *
 * FUNCTION:	initialize the incore inode map control structures for
 *		a fileset or aggregate init time.
 *
 *		the inode map's control structure (dinomap) is
 *		brought in from disk and placed in virtual memory.
 *
 * PARAMETERS:
 *	ipimap	- pointer to inode map inode for the aggregate or fileset.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOMEM	- insufficient free virtual memory.
 *	-EIO	- i/o error.
 */
int diMount(struct inode *ipimap)
{
	struct inomap *imap;
	struct metapage *mp;
	int index;
	struct dinomap_disk *dinom_le;

	/*
	 * allocate/initialize the in-memory inode map control structure
	 */
	/* allocate the in-memory inode map control structure. */
	imap = kmalloc(sizeof(struct inomap), GFP_KERNEL);
	if (imap == NULL) {
		jfs_err("diMount: kmalloc returned NULL!");
		return -ENOMEM;
	}

	/* read the on-disk inode map control structure. */

	mp = read_metapage(ipimap,
			   IMAPBLKNO << JFS_SBI(ipimap->i_sb)->l2nbperpage,
			   PSIZE, 0);
	if (mp == NULL) {
		kfree(imap);
		return -EIO;
	}

	/* copy the on-disk version to the in-memory version. */
	dinom_le = (struct dinomap_disk *) mp->data;
	imap->im_freeiag = le32_to_cpu(dinom_le->in_freeiag);
	imap->im_nextiag = le32_to_cpu(dinom_le->in_nextiag);
	atomic_set(&imap->im_numinos, le32_to_cpu(dinom_le->in_numinos));
	atomic_set(&imap->im_numfree, le32_to_cpu(dinom_le->in_numfree));
	imap->im_nbperiext = le32_to_cpu(dinom_le->in_nbperiext);
	imap->im_l2nbperiext = le32_to_cpu(dinom_le->in_l2nbperiext);
	for (index = 0; index < MAXAG; index++) {
		imap->im_agctl[index].inofree =
		    le32_to_cpu(dinom_le->in_agctl[index].inofree);
		imap->im_agctl[index].extfree =
		    le32_to_cpu(dinom_le->in_agctl[index].extfree);
		imap->im_agctl[index].numinos =
		    le32_to_cpu(dinom_le->in_agctl[index].numinos);
		imap->im_agctl[index].numfree =
		    le32_to_cpu(dinom_le->in_agctl[index].numfree);
	}

	/* release the buffer. */
	release_metapage(mp);

	/*
	 * allocate/initialize inode allocation map locks
	 */
	/* allocate and init iag free list lock */
	IAGFREE_LOCK_INIT(imap);

	/* allocate and init ag list locks */
	for (index = 0; index < MAXAG; index++) {
		AG_LOCK_INIT(imap, index);
	}

	/* bind the inode map inode and inode map control structure
	 * to each other.
	 */
	imap->im_ipimap = ipimap;
	JFS_IP(ipimap)->i_imap = imap;

	return (0);
}


/*
 * NAME:	diUnmount()
 *
 * FUNCTION:	write to disk the incore inode map control structures for
 *		a fileset or aggregate at unmount time.
 *
 * PARAMETERS:
 *	ipimap	- pointer to inode map inode for the aggregate or fileset.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOMEM	- insufficient free virtual memory.
 *	-EIO	- i/o error.
 */
int diUnmount(struct inode *ipimap, int mounterror)
{
	struct inomap *imap = JFS_IP(ipimap)->i_imap;

	/*
	 * update the on-disk inode map control structure
	 */

	if (!(mounterror || isReadOnly(ipimap)))
		diSync(ipimap);

	/*
	 * Invalidate the page cache buffers
	 */
	truncate_inode_pages(ipimap->i_mapping, 0);

	/*
	 * free in-memory control structure
	 */
	kfree(imap);

	return (0);
}


/*
 *	diSync()
 */
int diSync(struct inode *ipimap)
{
	struct dinomap_disk *dinom_le;
	struct inomap *imp = JFS_IP(ipimap)->i_imap;
	struct metapage *mp;
	int index;

	/*
	 * write imap global conrol page
	 */
	/* read the on-disk inode map control structure */
	mp = get_metapage(ipimap,
			  IMAPBLKNO << JFS_SBI(ipimap->i_sb)->l2nbperpage,
			  PSIZE, 0);
	if (mp == NULL) {
		jfs_err("diSync: get_metapage failed!");
		return -EIO;
	}

	/* copy the in-memory version to the on-disk version */
	dinom_le = (struct dinomap_disk *) mp->data;
	dinom_le->in_freeiag = cpu_to_le32(imp->im_freeiag);
	dinom_le->in_nextiag = cpu_to_le32(imp->im_nextiag);
	dinom_le->in_numinos = cpu_to_le32(atomic_read(&imp->im_numinos));
	dinom_le->in_numfree = cpu_to_le32(atomic_read(&imp->im_numfree));
	dinom_le->in_nbperiext = cpu_to_le32(imp->im_nbperiext);
	dinom_le->in_l2nbperiext = cpu_to_le32(imp->im_l2nbperiext);
	for (index = 0; index < MAXAG; index++) {
		dinom_le->in_agctl[index].inofree =
		    cpu_to_le32(imp->im_agctl[index].inofree);
		dinom_le->in_agctl[index].extfree =
		    cpu_to_le32(imp->im_agctl[index].extfree);
		dinom_le->in_agctl[index].numinos =
		    cpu_to_le32(imp->im_agctl[index].numinos);
		dinom_le->in_agctl[index].numfree =
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
 * FUNCTION:	initialize an incore inode from disk.
 *
 *		on entry, the specifed incore inode should itself
 *		specify the disk inode number corresponding to the
 *		incore inode (i.e. i_number should be initialized).
 *
 *		this routine handles incore inode initialization for
 *		both "special" and "regular" inodes.  special inodes
 *		are those required early in the mount process and
 *		require special handling since much of the file system
 *		is not yet initialized.  these "special" inodes are
 *		identified by a NULL inode map inode pointer and are
 *		actually initialized by a call to diReadSpecial().
 *
 *		for regular inodes, the iag describing the disk inode
 *		is read from disk to determine the inode extent address
 *		for the disk inode.  with the inode extent address in
 *		hand, the page of the extent that contains the disk
 *		inode is read and the disk inode is copied to the
 *		incore inode.
 *
 * PARAMETERS:
 *	ip	-  pointer to incore inode to be initialized from disk.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 *	-ENOMEM	- insufficient memory
 *
 */
int diRead(struct inode *ip)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	int iagno, ino, extno, rc;
	struct inode *ipimap;
	struct dinode *dp;
	struct iag *iagp;
	struct metapage *mp;
	s64 blkno, agstart;
	struct inomap *imap;
	int block_offset;
	int inodes_left;
	unsigned long pageno;
	int rel_inode;

	jfs_info("diRead: ino = %ld", ip->i_ino);

	ipimap = sbi->ipimap;
	JFS_IP(ip)->ipimap = ipimap;

	/* determine the iag number for this inode (number) */
	iagno = INOTOIAG(ip->i_ino);

	/* read the iag */
	imap = JFS_IP(ipimap)->i_imap;
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);
	rc = diIAGRead(imap, iagno, &mp);
	IREAD_UNLOCK(ipimap);
	if (rc) {
		jfs_err("diRead: diIAGRead returned %d", rc);
		return (rc);
	}

	iagp = (struct iag *) mp->data;

	/* determine inode extent that holds the disk inode */
	ino = ip->i_ino & (INOSPERIAG - 1);
	extno = ino >> L2INOSPEREXT;

	if ((lengthPXD(&iagp->inoext[extno]) != imap->im_nbperiext) ||
	    (addressPXD(&iagp->inoext[extno]) == 0)) {
		release_metapage(mp);
		return -ESTALE;
	}

	/* get disk block number of the page within the inode extent
	 * that holds the disk inode.
	 */
	blkno = INOPBLK(&iagp->inoext[extno], ino, sbi->l2nbperpage);

	/* get the ag for the iag */
	agstart = le64_to_cpu(iagp->agstart);

	release_metapage(mp);

	rel_inode = (ino & (INOSPERPAGE - 1));
	pageno = blkno >> sbi->l2nbperpage;

	if ((block_offset = ((u32) blkno & (sbi->nbperpage - 1)))) {
		/*
		 * OS/2 didn't always align inode extents on page boundaries
		 */
		inodes_left =
		     (sbi->nbperpage - block_offset) << sbi->l2niperblk;

		if (rel_inode < inodes_left)
			rel_inode += block_offset << sbi->l2niperblk;
		else {
			pageno += 1;
			rel_inode -= inodes_left;
		}
	}

	/* read the page of disk inode */
	mp = read_metapage(ipimap, pageno << sbi->l2nbperpage, PSIZE, 1);
	if (!mp) {
		jfs_err("diRead: read_metapage failed");
		return -EIO;
	}

	/* locate the disk inode requested */
	dp = (struct dinode *) mp->data;
	dp += rel_inode;

	if (ip->i_ino != le32_to_cpu(dp->di_number)) {
		jfs_error(ip->i_sb, "diRead: i_ino != di_number");
		rc = -EIO;
	} else if (le32_to_cpu(dp->di_nlink) == 0)
		rc = -ESTALE;
	else
		/* copy the disk inode to the in-memory inode */
		rc = copy_from_dinode(dp, ip);

	release_metapage(mp);

	/* set the ag for the inode */
	JFS_IP(ip)->agno = BLKTOAG(agstart, sbi);
	JFS_IP(ip)->active_ag = -1;

	return (rc);
}


/*
 * NAME:	diReadSpecial()
 *
 * FUNCTION:	initialize a 'special' inode from disk.
 *
 *		this routines handles aggregate level inodes.  The
 *		inode cache cannot differentiate between the
 *		aggregate inodes and the filesystem inodes, so we
 *		handle these here.  We don't actually use the aggregate
 *		inode map, since these inodes are at a fixed location
 *		and in some cases the aggregate inode map isn't initialized
 *		yet.
 *
 * PARAMETERS:
 *	sb - filesystem superblock
 *	inum - aggregate inode number
 *	secondary - 1 if secondary aggregate inode table
 *
 * RETURN VALUES:
 *	new inode	- success
 *	NULL		- i/o error.
 */
struct inode *diReadSpecial(struct super_block *sb, ino_t inum, int secondary)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	uint address;
	struct dinode *dp;
	struct inode *ip;
	struct metapage *mp;

	ip = new_inode(sb);
	if (ip == NULL) {
		jfs_err("diReadSpecial: new_inode returned NULL!");
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

	ip->i_ino = inum;

	address += inum >> 3;	/* 8 inodes per 4K page */

	/* read the page of fixed disk inode (AIT) in raw mode */
	mp = read_metapage(ip, address << sbi->l2nbperpage, PSIZE, 1);
	if (mp == NULL) {
		ip->i_nlink = 1;	/* Don't want iput() deleting it */
		iput(ip);
		return (NULL);
	}

	/* get the pointer to the disk inode of interest */
	dp = (struct dinode *) (mp->data);
	dp += inum % 8;		/* 8 inodes per 4K page */

	/* copy on-disk inode to in-memory inode */
	if ((copy_from_dinode(dp, ip)) != 0) {
		/* handle bad return by returning NULL for ip */
		ip->i_nlink = 1;	/* Don't want iput() deleting it */
		iput(ip);
		/* release the page */
		release_metapage(mp);
		return (NULL);

	}

	ip->i_mapping->a_ops = &jfs_metapage_aops;
	mapping_set_gfp_mask(ip->i_mapping, GFP_NOFS);

	/* Allocations to metadata inodes should not affect quotas */
	ip->i_flags |= S_NOQUOTA;

	if ((inum == FILESYSTEM_I) && (JFS_IP(ip)->ipimap == sbi->ipaimap)) {
		sbi->gengen = le32_to_cpu(dp->di_gengen);
		sbi->inostamp = le32_to_cpu(dp->di_inostamp);
	}

	/* release the page */
	release_metapage(mp);

	/*
	 * __mark_inode_dirty expects inodes to be hashed.  Since we don't
	 * want special inodes in the fileset inode space, we make them
	 * appear hashed, but do not put on any lists.  hlist_del()
	 * will work fine and require no locking.
	 */
	hlist_add_fake(&ip->i_hash);

	return (ip);
}

/*
 * NAME:	diWriteSpecial()
 *
 * FUNCTION:	Write the special inode to disk
 *
 * PARAMETERS:
 *	ip - special inode
 *	secondary - 1 if secondary aggregate inode table
 *
 * RETURN VALUES: none
 */

void diWriteSpecial(struct inode *ip, int secondary)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	uint address;
	struct dinode *dp;
	ino_t inum = ip->i_ino;
	struct metapage *mp;

	if (secondary)
		address = addressPXD(&sbi->ait2) >> sbi->l2nbperpage;
	else
		address = AITBL_OFF >> L2PSIZE;

	ASSERT(inum < INOSPEREXT);

	address += inum >> 3;	/* 8 inodes per 4K page */

	/* read the page of fixed disk inode (AIT) in raw mode */
	mp = read_metapage(ip, address << sbi->l2nbperpage, PSIZE, 1);
	if (mp == NULL) {
		jfs_err("diWriteSpecial: failed to read aggregate inode "
			"extent!");
		return;
	}

	/* get the pointer to the disk inode of interest */
	dp = (struct dinode *) (mp->data);
	dp += inum % 8;		/* 8 inodes per 4K page */

	/* copy on-disk inode to in-memory inode */
	copy_to_dinode(dp, ip);
	memcpy(&dp->di_xtroot, &JFS_IP(ip)->i_xtroot, 288);

	if (inum == FILESYSTEM_I)
		dp->di_gengen = cpu_to_le32(sbi->gengen);

	/* write the page */
	write_metapage(mp);
}

/*
 * NAME:	diFreeSpecial()
 *
 * FUNCTION:	Free allocated space for special inode
 */
void diFreeSpecial(struct inode *ip)
{
	if (ip == NULL) {
		jfs_err("diFreeSpecial called with NULL ip!");
		return;
	}
	filemap_write_and_wait(ip->i_mapping);
	truncate_inode_pages(ip->i_mapping, 0);
	iput(ip);
}



/*
 * NAME:	diWrite()
 *
 * FUNCTION:	write the on-disk inode portion of the in-memory inode
 *		to its corresponding on-disk inode.
 *
 *		on entry, the specifed incore inode should itself
 *		specify the disk inode number corresponding to the
 *		incore inode (i.e. i_number should be initialized).
 *
 *		the inode contains the inode extent address for the disk
 *		inode.  with the inode extent address in hand, the
 *		page of the extent that contains the disk inode is
 *		read and the disk inode portion of the incore inode
 *		is copied to the disk inode.
 *
 * PARAMETERS:
 *	tid -  transacation id
 *	ip  -  pointer to incore inode to be written to the inode extent.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 */
int diWrite(tid_t tid, struct inode *ip)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);
	int rc = 0;
	s32 ino;
	struct dinode *dp;
	s64 blkno;
	int block_offset;
	int inodes_left;
	struct metapage *mp;
	unsigned long pageno;
	int rel_inode;
	int dioffset;
	struct inode *ipimap;
	uint type;
	lid_t lid;
	struct tlock *ditlck, *tlck;
	struct linelock *dilinelock, *ilinelock;
	struct lv *lv;
	int n;

	ipimap = jfs_ip->ipimap;

	ino = ip->i_ino & (INOSPERIAG - 1);

	if (!addressPXD(&(jfs_ip->ixpxd)) ||
	    (lengthPXD(&(jfs_ip->ixpxd)) !=
	     JFS_IP(ipimap)->i_imap->im_nbperiext)) {
		jfs_error(ip->i_sb, "diWrite: ixpxd invalid");
		return -EIO;
	}

	/*
	 * read the page of disk inode containing the specified inode:
	 */
	/* compute the block address of the page */
	blkno = INOPBLK(&(jfs_ip->ixpxd), ino, sbi->l2nbperpage);

	rel_inode = (ino & (INOSPERPAGE - 1));
	pageno = blkno >> sbi->l2nbperpage;

	if ((block_offset = ((u32) blkno & (sbi->nbperpage - 1)))) {
		/*
		 * OS/2 didn't always align inode extents on page boundaries
		 */
		inodes_left =
		    (sbi->nbperpage - block_offset) << sbi->l2niperblk;

		if (rel_inode < inodes_left)
			rel_inode += block_offset << sbi->l2niperblk;
		else {
			pageno += 1;
			rel_inode -= inodes_left;
		}
	}
	/* read the page of disk inode */
      retry:
	mp = read_metapage(ipimap, pageno << sbi->l2nbperpage, PSIZE, 1);
	if (!mp)
		return -EIO;

	/* get the pointer to the disk inode */
	dp = (struct dinode *) mp->data;
	dp += rel_inode;

	dioffset = (ino & (INOSPERPAGE - 1)) << L2DISIZE;

	/*
	 * acquire transaction lock on the on-disk inode;
	 * N.B. tlock is acquired on ipimap not ip;
	 */
	if ((ditlck =
	     txLock(tid, ipimap, mp, tlckINODE | tlckENTRY)) == NULL)
		goto retry;
	dilinelock = (struct linelock *) & ditlck->lock;

	/*
	 * copy btree root from in-memory inode to on-disk inode
	 *
	 * (tlock is taken from inline B+-tree root in in-memory
	 * inode when the B+-tree root is updated, which is pointed
	 * by jfs_ip->blid as well as being on tx tlock list)
	 *
	 * further processing of btree root is based on the copy
	 * in in-memory inode, where txLog() will log from, and,
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
		 * copy xtree root from inode to dinode:
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
		 * copy xtree root from inode to dinode:
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
		 * copy dtree root from inode to dinode:
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
	 * copy inline symlink from in-memory inode to on-disk inode
	 */
	if (S_ISLNK(ip->i_mode) && ip->i_size < IDATASIZE) {
		lv = & dilinelock->lv[dilinelock->index];
		lv->offset = (dioffset + 2 * 128) >> L2INODESLOTSIZE;
		lv->length = 2;
		memcpy(&dp->di_fastsymlink, jfs_ip->i_inline, IDATASIZE);
		dilinelock->index++;
	}
	/*
	 * copy inline data from in-memory inode to on-disk inode:
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
	 *	lock/copy inode base: 128 byte slot granularity
	 */
	lv = & dilinelock->lv[dilinelock->index];
	lv->offset = dioffset >> L2INODESLOTSIZE;
	copy_to_dinode(dp, ip);
	if (test_and_clear_cflag(COMMIT_Dirtable, ip)) {
		lv->length = 2;
		memcpy(&dp->di_dirtable, &jfs_ip->i_dirtable, 96);
	} else
		lv->length = 1;
	dilinelock->index++;

	/* release the buffer holding the updated on-disk inode.
	 * the buffer will be later written by commit processing.
	 */
	write_metapage(mp);

	return (rc);
}


/*
 * NAME:	diFree(ip)
 *
 * FUNCTION:	free a specified inode from the inode working map
 *		for a fileset or aggregate.
 *
 *		if the inode to be freed represents the first (only)
 *		free inode within the iag, the iag will be placed on
 *		the ag free inode list.
 *
 *		freeing the inode will cause the inode extent to be
 *		freed if the inode is the only allocated inode within
 *		the extent.  in this case all the disk resource backing
 *		up the inode extent will be freed. in addition, the iag
 *		will be placed on the ag extent free list if the extent
 *		is the first free extent in the iag.  if freeing the
 *		extent also means that no free inodes will exist for
 *		the iag, the iag will also be removed from the ag free
 *		inode list.
 *
 *		the iag describing the inode will be freed if the extent
 *		is to be freed and it is the only backed extent within
 *		the iag.  in this case, the iag will be removed from the
 *		ag free extent list and ag free inode list and placed on
 *		the inode map's free iag list.
 *
 *		a careful update approach is used to provide consistency
 *		in the face of updates to multiple buffers.  under this
 *		approach, all required buffers are obtained before making
 *		any updates and are held until all updates are complete.
 *
 * PARAMETERS:
 *	ip	- inode to be freed.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 */
int diFree(struct inode *ip)
{
	int rc;
	ino_t inum = ip->i_ino;
	struct iag *iagp, *aiagp, *biagp, *ciagp, *diagp;
	struct metapage *mp, *amp, *bmp, *cmp, *dmp;
	int iagno, ino, extno, bitno, sword, agno;
	int back, fwd;
	u32 bitmap, mask;
	struct inode *ipimap = JFS_SBI(ip->i_sb)->ipimap;
	struct inomap *imap = JFS_IP(ipimap)->i_imap;
	pxd_t freepxd;
	tid_t tid;
	struct inode *iplist[3];
	struct tlock *tlck;
	struct pxd_lock *pxdlock;

	/*
	 * This is just to suppress compiler warnings.  The same logic that
	 * references these variables is used to initialize them.
	 */
	aiagp = biagp = ciagp = diagp = NULL;

	/* get the iag number containing the inode.
	 */
	iagno = INOTOIAG(inum);

	/* make sure that the iag is contained within
	 * the map.
	 */
	if (iagno >= imap->im_nextiag) {
		print_hex_dump(KERN_ERR, "imap: ", DUMP_PREFIX_ADDRESS, 16, 4,
			       imap, 32, 0);
		jfs_error(ip->i_sb,
			  "diFree: inum = %d, iagno = %d, nextiag = %d",
			  (uint) inum, iagno, imap->im_nextiag);
		return -EIO;
	}

	/* get the allocation group for this ino.
	 */
	agno = JFS_IP(ip)->agno;

	/* Lock the AG specific inode map information
	 */
	AG_LOCK(imap, agno);

	/* Obtain read lock in imap inode.  Don't release it until we have
	 * read all of the IAG's that we are going to.
	 */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);

	/* read the iag.
	 */
	if ((rc = diIAGRead(imap, iagno, &mp))) {
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, agno);
		return (rc);
	}
	iagp = (struct iag *) mp->data;

	/* get the inode number and extent number of the inode within
	 * the iag and the inode number within the extent.
	 */
	ino = inum & (INOSPERIAG - 1);
	extno = ino >> L2INOSPEREXT;
	bitno = ino & (INOSPEREXT - 1);
	mask = HIGHORDER >> bitno;

	if (!(le32_to_cpu(iagp->wmap[extno]) & mask)) {
		jfs_error(ip->i_sb,
			  "diFree: wmap shows inode already free");
	}

	if (!addressPXD(&iagp->inoext[extno])) {
		release_metapage(mp);
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, agno);
		jfs_error(ip->i_sb, "diFree: invalid inoext");
		return -EIO;
	}

	/* compute the bitmap for the extent reflecting the freed inode.
	 */
	bitmap = le32_to_cpu(iagp->wmap[extno]) & ~mask;

	if (imap->im_agctl[agno].numfree > imap->im_agctl[agno].numinos) {
		release_metapage(mp);
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, agno);
		jfs_error(ip->i_sb, "diFree: numfree > numinos");
		return -EIO;
	}
	/*
	 *	inode extent still has some inodes or below low water mark:
	 *	keep the inode extent;
	 */
	if (bitmap ||
	    imap->im_agctl[agno].numfree < 96 ||
	    (imap->im_agctl[agno].numfree < 288 &&
	     (((imap->im_agctl[agno].numfree * 100) /
	       imap->im_agctl[agno].numinos) <= 25))) {
		/* if the iag currently has no free inodes (i.e.,
		 * the inode being freed is the first free inode of iag),
		 * insert the iag at head of the inode free list for the ag.
		 */
		if (iagp->nfreeinos == 0) {
			/* check if there are any iags on the ag inode
			 * free list.  if so, read the first one so that
			 * we can link the current iag onto the list at
			 * the head.
			 */
			if ((fwd = imap->im_agctl[agno].inofree) >= 0) {
				/* read the iag that currently is the head
				 * of the list.
				 */
				if ((rc = diIAGRead(imap, fwd, &amp))) {
					IREAD_UNLOCK(ipimap);
					AG_UNLOCK(imap, agno);
					release_metapage(mp);
					return (rc);
				}
				aiagp = (struct iag *) amp->data;

				/* make current head point back to the iag.
				 */
				aiagp->inofreeback = cpu_to_le32(iagno);

				write_metapage(amp);
			}

			/* iag points forward to current head and iag
			 * becomes the new head of the list.
			 */
			iagp->inofreefwd =
			    cpu_to_le32(imap->im_agctl[agno].inofree);
			iagp->inofreeback = cpu_to_le32(-1);
			imap->im_agctl[agno].inofree = iagno;
		}
		IREAD_UNLOCK(ipimap);

		/* update the free inode summary map for the extent if
		 * freeing the inode means the extent will now have free
		 * inodes (i.e., the inode being freed is the first free
		 * inode of extent),
		 */
		if (iagp->wmap[extno] == cpu_to_le32(ONES)) {
			sword = extno >> L2EXTSPERSUM;
			bitno = extno & (EXTSPERSUM - 1);
			iagp->inosmap[sword] &=
			    cpu_to_le32(~(HIGHORDER >> bitno));
		}

		/* update the bitmap.
		 */
		iagp->wmap[extno] = cpu_to_le32(bitmap);

		/* update the free inode counts at the iag, ag and
		 * map level.
		 */
		le32_add_cpu(&iagp->nfreeinos, 1);
		imap->im_agctl[agno].numfree += 1;
		atomic_inc(&imap->im_numfree);

		/* release the AG inode map lock
		 */
		AG_UNLOCK(imap, agno);

		/* write the iag */
		write_metapage(mp);

		return (0);
	}


	/*
	 *	inode extent has become free and above low water mark:
	 *	free the inode extent;
	 */

	/*
	 *	prepare to update iag list(s) (careful update step 1)
	 */
	amp = bmp = cmp = dmp = NULL;
	fwd = back = -1;

	/* check if the iag currently has no free extents.  if so,
	 * it will be placed on the head of the ag extent free list.
	 */
	if (iagp->nfreeexts == 0) {
		/* check if the ag extent free list has any iags.
		 * if so, read the iag at the head of the list now.
		 * this (head) iag will be updated later to reflect
		 * the addition of the current iag at the head of
		 * the list.
		 */
		if ((fwd = imap->im_agctl[agno].extfree) >= 0) {
			if ((rc = diIAGRead(imap, fwd, &amp)))
				goto error_out;
			aiagp = (struct iag *) amp->data;
		}
	} else {
		/* iag has free extents. check if the addition of a free
		 * extent will cause all extents to be free within this
		 * iag.  if so, the iag will be removed from the ag extent
		 * free list and placed on the inode map's free iag list.
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

	/* remove the iag from the ag inode free list if freeing
	 * this extent cause the iag to have no free inodes.
	 */
	if (iagp->nfreeinos == cpu_to_le32(INOSPEREXT - 1)) {
		int inofreeback = le32_to_cpu(iagp->inofreeback);
		int inofreefwd = le32_to_cpu(iagp->inofreefwd);

		/* in preparation for removing the iag from the
		 * ag inode free list, read the iags preceding
		 * and following the iag on the ag inode free
		 * list.  before reading these iags, we must make
		 * sure that we already don't have them in hand
		 * from up above, since re-reading an iag (buffer)
		 * we are currently holding would cause a deadlock.
		 */
		if (inofreefwd >= 0) {

			if (inofreefwd == fwd)
				ciagp = (struct iag *) amp->data;
			else if (inofreefwd == back)
				ciagp = (struct iag *) bmp->data;
			else {
				if ((rc =
				     diIAGRead(imap, inofreefwd, &cmp)))
					goto error_out;
				ciagp = (struct iag *) cmp->data;
			}
			assert(ciagp != NULL);
		}

		if (inofreeback >= 0) {
			if (inofreeback == fwd)
				diagp = (struct iag *) amp->data;
			else if (inofreeback == back)
				diagp = (struct iag *) bmp->data;
			else {
				if ((rc =
				     diIAGRead(imap, inofreeback, &dmp)))
					goto error_out;
				diagp = (struct iag *) dmp->data;
			}
			assert(diagp != NULL);
		}
	}

	IREAD_UNLOCK(ipimap);

	/*
	 * invalidate any page of the inode extent freed from buffer cache;
	 */
	freepxd = iagp->inoext[extno];
	invalidate_pxd_metapages(ip, freepxd);

	/*
	 *	update iag list(s) (careful update step 2)
	 */
	/* add the iag to the ag extent free list if this is the
	 * first free extent for the iag.
	 */
	if (iagp->nfreeexts == 0) {
		if (fwd >= 0)
			aiagp->extfreeback = cpu_to_le32(iagno);

		iagp->extfreefwd =
		    cpu_to_le32(imap->im_agctl[agno].extfree);
		iagp->extfreeback = cpu_to_le32(-1);
		imap->im_agctl[agno].extfree = iagno;
	} else {
		/* remove the iag from the ag extent list if all extents
		 * are now free and place it on the inode map iag free list.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG - 1)) {
			if (fwd >= 0)
				aiagp->extfreeback = iagp->extfreeback;

			if (back >= 0)
				biagp->extfreefwd = iagp->extfreefwd;
			else
				imap->im_agctl[agno].extfree =
				    le32_to_cpu(iagp->extfreefwd);

			iagp->extfreefwd = iagp->extfreeback = cpu_to_le32(-1);

			IAGFREE_LOCK(imap);
			iagp->iagfree = cpu_to_le32(imap->im_freeiag);
			imap->im_freeiag = iagno;
			IAGFREE_UNLOCK(imap);
		}
	}

	/* remove the iag from the ag inode free list if freeing
	 * this extent causes the iag to have no free inodes.
	 */
	if (iagp->nfreeinos == cpu_to_le32(INOSPEREXT - 1)) {
		if ((int) le32_to_cpu(iagp->inofreefwd) >= 0)
			ciagp->inofreeback = iagp->inofreeback;

		if ((int) le32_to_cpu(iagp->inofreeback) >= 0)
			diagp->inofreefwd = iagp->inofreefwd;
		else
			imap->im_agctl[agno].inofree =
			    le32_to_cpu(iagp->inofreefwd);

		iagp->inofreefwd = iagp->inofreeback = cpu_to_le32(-1);
	}

	/* update the inode extent address and working map
	 * to reflect the free extent.
	 * the permanent map should have been updated already
	 * for the inode being freed.
	 */
	if (iagp->pmap[extno] != 0) {
		jfs_error(ip->i_sb, "diFree: the pmap does not show inode free");
	}
	iagp->wmap[extno] = 0;
	PXDlength(&iagp->inoext[extno], 0);
	PXDaddress(&iagp->inoext[extno], 0);

	/* update the free extent and free inode summary maps
	 * to reflect the freed extent.
	 * the inode summary map is marked to indicate no inodes
	 * available for the freed extent.
	 */
	sword = extno >> L2EXTSPERSUM;
	bitno = extno & (EXTSPERSUM - 1);
	mask = HIGHORDER >> bitno;
	iagp->inosmap[sword] |= cpu_to_le32(mask);
	iagp->extsmap[sword] &= cpu_to_le32(~mask);

	/* update the number of free inodes and number of free extents
	 * for the iag.
	 */
	le32_add_cpu(&iagp->nfreeinos, -(INOSPEREXT - 1));
	le32_add_cpu(&iagp->nfreeexts, 1);

	/* update the number of free inodes and backed inodes
	 * at the ag and inode map level.
	 */
	imap->im_agctl[agno].numfree -= (INOSPEREXT - 1);
	imap->im_agctl[agno].numinos -= INOSPEREXT;
	atomic_sub(INOSPEREXT - 1, &imap->im_numfree);
	atomic_sub(INOSPEREXT, &imap->im_numinos);

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
	 * for the inode extent freed;
	 *
	 * N.B. AG_LOCK is released and iag will be released below, and
	 * other thread may allocate inode from/reusing the ixad freed
	 * BUT with new/different backing inode extent from the extent
	 * to be freed by the transaction;
	 */
	tid = txBegin(ipimap->i_sb, COMMIT_FORCE);
	mutex_lock(&JFS_IP(ipimap)->commit_mutex);

	/* acquire tlock of the iag page of the freed ixad
	 * to force the page NOHOMEOK (even though no data is
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
	 * It's not pretty, but it works.
	 */
	iplist[1] = (struct inode *) (size_t)iagno;
	iplist[2] = (struct inode *) (size_t)extno;

	rc = txCommit(tid, 1, &iplist[0], COMMIT_FORCE);

	txEnd(tid);
	mutex_unlock(&JFS_IP(ipimap)->commit_mutex);

	/* unlock the AG inode map information */
	AG_UNLOCK(imap, agno);

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

	AG_UNLOCK(imap, agno);

	release_metapage(mp);

	return (rc);
}

/*
 * There are several places in the diAlloc* routines where we initialize
 * the inode.
 */
static inline void
diInitInode(struct inode *ip, int iagno, int ino, int extno, struct iag * iagp)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);

	ip->i_ino = (iagno << L2INOSPERIAG) + ino;
	jfs_ip->ixpxd = iagp->inoext[extno];
	jfs_ip->agno = BLKTOAG(le64_to_cpu(iagp->agstart), sbi);
	jfs_ip->active_ag = -1;
}


/*
 * NAME:	diAlloc(pip,dir,ip)
 *
 * FUNCTION:	allocate a disk inode from the inode working map
 *		for a fileset or aggregate.
 *
 * PARAMETERS:
 *	pip	- pointer to incore inode for the parent inode.
 *	dir	- 'true' if the new disk inode is for a directory.
 *	ip	- pointer to a new inode
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
int diAlloc(struct inode *pip, bool dir, struct inode *ip)
{
	int rc, ino, iagno, addext, extno, bitno, sword;
	int nwords, rem, i, agno;
	u32 mask, inosmap, extsmap;
	struct inode *ipimap;
	struct metapage *mp;
	ino_t inum;
	struct iag *iagp;
	struct inomap *imap;

	/* get the pointers to the inode map inode and the
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
		agno = dbNextAG(JFS_SBI(pip->i_sb)->ipbmap);
		AG_LOCK(imap, agno);
		goto tryag;
	}

	/* for files, the policy starts off by trying to allocate from
	 * the same iag containing the parent disk inode:
	 * try to allocate the new disk inode close to the parent disk
	 * inode, using parent disk inode number + 1 as the allocation
	 * hint.  (we use a left-to-right policy to attempt to avoid
	 * moving backward on the disk.)  compute the hint within the
	 * file system and the iag.
	 */

	/* get the ag number of this iag */
	agno = JFS_IP(pip)->agno;

	if (atomic_read(&JFS_SBI(pip->i_sb)->bmap->db_active[agno])) {
		/*
		 * There is an open file actively growing.  We want to
		 * allocate new inodes from a different ag to avoid
		 * fragmentation problems.
		 */
		agno = dbNextAG(JFS_SBI(pip->i_sb)->ipbmap);
		AG_LOCK(imap, agno);
		goto tryag;
	}

	inum = pip->i_ino + 1;
	ino = inum & (INOSPERIAG - 1);

	/* back off the hint if it is outside of the iag */
	if (ino == 0)
		inum = pip->i_ino;

	/* lock the AG inode map information */
	AG_LOCK(imap, agno);

	/* Get read lock on imap inode */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);

	/* get the iag number and read the iag */
	iagno = INOTOIAG(inum);
	if ((rc = diIAGRead(imap, iagno, &mp))) {
		IREAD_UNLOCK(ipimap);
		AG_UNLOCK(imap, agno);
		return (rc);
	}
	iagp = (struct iag *) mp->data;

	/* determine if new inode extent is allowed to be added to the iag.
	 * new inode extent can be added to the iag if the ag
	 * has less than 32 free disk inodes and the iag has free extents.
	 */
	addext = (imap->im_agctl[agno].numfree < 32 && iagp->nfreeexts);

	/*
	 *	try to allocate from the IAG
	 */
	/* check if the inode may be allocated from the iag
	 * (i.e. the inode has free inodes or new extent can be added).
	 */
	if (iagp->nfreeinos || addext) {
		/* determine the extent number of the hint.
		 */
		extno = ino >> L2INOSPEREXT;

		/* check if the extent containing the hint has backed
		 * inodes.  if so, try to allocate within this extent.
		 */
		if (addressPXD(&iagp->inoext[extno])) {
			bitno = ino & (INOSPEREXT - 1);
			if ((bitno =
			     diFindFree(le32_to_cpu(iagp->wmap[extno]),
					bitno))
			    < INOSPEREXT) {
				ino = (extno << L2INOSPEREXT) + bitno;

				/* a free inode (bit) was found within this
				 * extent, so allocate it.
				 */
				rc = diAllocBit(imap, iagp, ino);
				IREAD_UNLOCK(ipimap);
				if (rc) {
					assert(rc == -EIO);
				} else {
					/* set the results of the allocation
					 * and write the iag.
					 */
					diInitInode(ip, iagno, ino, extno,
						    iagp);
					mark_metapage_dirty(mp);
				}
				release_metapage(mp);

				/* free the AG lock and return.
				 */
				AG_UNLOCK(imap, agno);
				return (rc);
			}

			if (!addext)
				extno =
				    (extno ==
				     EXTSPERIAG - 1) ? 0 : extno + 1;
		}

		/*
		 * no free inodes within the extent containing the hint.
		 *
		 * try to allocate from the backed extents following
		 * hint or, if appropriate (i.e. addext is true), allocate
		 * an extent of free inodes at or following the extent
		 * containing the hint.
		 *
		 * the free inode and free extent summary maps are used
		 * here, so determine the starting summary map position
		 * and the number of words we'll have to examine.  again,
		 * the approach is to allocate following the hint, so we
		 * might have to initially ignore prior bits of the summary
		 * map that represent extents prior to the extent containing
		 * the hint and later revisit these bits.
		 */
		bitno = extno & (EXTSPERSUM - 1);
		nwords = (bitno == 0) ? SMAPSZ : SMAPSZ + 1;
		sword = extno >> L2EXTSPERSUM;

		/* mask any prior bits for the starting words of the
		 * summary map.
		 */
		mask = ONES << (EXTSPERSUM - bitno);
		inosmap = le32_to_cpu(iagp->inosmap[sword]) | mask;
		extsmap = le32_to_cpu(iagp->extsmap[sword]) | mask;

		/* scan the free inode and free extent summary maps for
		 * free resources.
		 */
		for (i = 0; i < nwords; i++) {
			/* check if this word of the free inode summary
			 * map describes an extent with free inodes.
			 */
			if (~inosmap) {
				/* an extent with free inodes has been
				 * found. determine the extent number
				 * and the inode number within the extent.
				 */
				rem = diFindFree(inosmap, 0);
				extno = (sword << L2EXTSPERSUM) + rem;
				rem = diFindFree(le32_to_cpu(iagp->wmap[extno]),
						 0);
				if (rem >= INOSPEREXT) {
					IREAD_UNLOCK(ipimap);
					release_metapage(mp);
					AG_UNLOCK(imap, agno);
					jfs_error(ip->i_sb,
						  "diAlloc: can't find free bit "
						  "in wmap");
					return -EIO;
				}

				/* determine the inode number within the
				 * iag and allocate the inode from the
				 * map.
				 */
				ino = (extno << L2INOSPEREXT) + rem;
				rc = diAllocBit(imap, iagp, ino);
				IREAD_UNLOCK(ipimap);
				if (rc)
					assert(rc == -EIO);
				else {
					/* set the results of the allocation
					 * and write the iag.
					 */
					diInitInode(ip, iagno, ino, extno,
						    iagp);
					mark_metapage_dirty(mp);
				}
				release_metapage(mp);

				/* free the AG lock and return.
				 */
				AG_UNLOCK(imap, agno);
				return (rc);

			}

			/* check if we may allocate an extent of free
			 * inodes and whether this word of the free
			 * extents summary map describes a free extent.
			 */
			if (addext && ~extsmap) {
				/* a free extent has been found.  determine
				 * the extent number.
				 */
				rem = diFindFree(extsmap, 0);
				extno = (sword << L2EXTSPERSUM) + rem;

				/* allocate an extent of free inodes.
				 */
				if ((rc = diNewExt(imap, iagp, extno))) {
					/* if there is no disk space for a
					 * new extent, try to allocate the
					 * disk inode from somewhere else.
					 */
					if (rc == -ENOSPC)
						break;

					assert(rc == -EIO);
				} else {
					/* set the results of the allocation
					 * and write the iag.
					 */
					diInitInode(ip, iagno,
						    extno << L2INOSPEREXT,
						    extno, iagp);
					mark_metapage_dirty(mp);
				}
				release_metapage(mp);
				/* free the imap inode & the AG lock & return.
				 */
				IREAD_UNLOCK(ipimap);
				AG_UNLOCK(imap, agno);
				return (rc);
			}

			/* move on to the next set of summary map words.
			 */
			sword = (sword == SMAPSZ - 1) ? 0 : sword + 1;
			inosmap = le32_to_cpu(iagp->inosmap[sword]);
			extsmap = le32_to_cpu(iagp->extsmap[sword]);
		}
	}
	/* unlock imap inode */
	IREAD_UNLOCK(ipimap);

	/* nothing doing in this iag, so release it. */
	release_metapage(mp);

      tryag:
	/*
	 * try to allocate anywhere within the same AG as the parent inode.
	 */
	rc = diAllocAG(imap, agno, dir, ip);

	AG_UNLOCK(imap, agno);

	if (rc != -ENOSPC)
		return (rc);

	/*
	 * try to allocate in any AG.
	 */
	return (diAllocAny(imap, agno, dir, ip));
}


/*
 * NAME:	diAllocAG(imap,agno,dir,ip)
 *
 * FUNCTION:	allocate a disk inode from the allocation group.
 *
 *		this routine first determines if a new extent of free
 *		inodes should be added for the allocation group, with
 *		the current request satisfied from this extent. if this
 *		is the case, an attempt will be made to do just that.  if
 *		this attempt fails or it has been determined that a new
 *		extent should not be added, an attempt is made to satisfy
 *		the request by allocating an existing (backed) free inode
 *		from the allocation group.
 *
 * PRE CONDITION: Already have the AG lock for this AG.
 *
 * PARAMETERS:
 *	imap	- pointer to inode map control structure.
 *	agno	- allocation group to allocate from.
 *	dir	- 'true' if the new disk inode is for a directory.
 *	ip	- pointer to the new inode to be filled in on successful return
 *		  with the disk inode number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int
diAllocAG(struct inomap * imap, int agno, bool dir, struct inode *ip)
{
	int rc, addext, numfree, numinos;

	/* get the number of free and the number of backed disk
	 * inodes currently within the ag.
	 */
	numfree = imap->im_agctl[agno].numfree;
	numinos = imap->im_agctl[agno].numinos;

	if (numfree > numinos) {
		jfs_error(ip->i_sb, "diAllocAG: numfree > numinos");
		return -EIO;
	}

	/* determine if we should allocate a new extent of free inodes
	 * within the ag: for directory inodes, add a new extent
	 * if there are a small number of free inodes or number of free
	 * inodes is a small percentage of the number of backed inodes.
	 */
	if (dir)
		addext = (numfree < 64 ||
			  (numfree < 256
			   && ((numfree * 100) / numinos) <= 20));
	else
		addext = (numfree == 0);

	/*
	 * try to allocate a new extent of free inodes.
	 */
	if (addext) {
		/* if free space is not available for this new extent, try
		 * below to allocate a free and existing (already backed)
		 * inode from the ag.
		 */
		if ((rc = diAllocExt(imap, agno, ip)) != -ENOSPC)
			return (rc);
	}

	/*
	 * try to allocate an existing free inode from the ag.
	 */
	return (diAllocIno(imap, agno, ip));
}


/*
 * NAME:	diAllocAny(imap,agno,dir,iap)
 *
 * FUNCTION:	allocate a disk inode from any other allocation group.
 *
 *		this routine is called when an allocation attempt within
 *		the primary allocation group has failed. if attempts to
 *		allocate an inode from any allocation group other than the
 *		specified primary group.
 *
 * PARAMETERS:
 *	imap	- pointer to inode map control structure.
 *	agno	- primary allocation group (to avoid).
 *	dir	- 'true' if the new disk inode is for a directory.
 *	ip	- pointer to a new inode to be filled in on successful return
 *		  with the disk inode number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int
diAllocAny(struct inomap * imap, int agno, bool dir, struct inode *ip)
{
	int ag, rc;
	int maxag = JFS_SBI(imap->im_ipimap->i_sb)->bmap->db_maxag;


	/* try to allocate from the ags following agno up to
	 * the maximum ag number.
	 */
	for (ag = agno + 1; ag <= maxag; ag++) {
		AG_LOCK(imap, ag);

		rc = diAllocAG(imap, ag, dir, ip);

		AG_UNLOCK(imap, ag);

		if (rc != -ENOSPC)
			return (rc);
	}

	/* try to allocate from the ags in front of agno.
	 */
	for (ag = 0; ag < agno; ag++) {
		AG_LOCK(imap, ag);

		rc = diAllocAG(imap, ag, dir, ip);

		AG_UNLOCK(imap, ag);

		if (rc != -ENOSPC)
			return (rc);
	}

	/* no free disk inodes.
	 */
	return -ENOSPC;
}


/*
 * NAME:	diAllocIno(imap,agno,ip)
 *
 * FUNCTION:	allocate a disk inode from the allocation group's free
 *		inode list, returning an error if this free list is
 *		empty (i.e. no iags on the list).
 *
 *		allocation occurs from the first iag on the list using
 *		the iag's free inode summary map to find the leftmost
 *		free inode in the iag.
 *
 * PRE CONDITION: Already have AG lock for this AG.
 *
 * PARAMETERS:
 *	imap	- pointer to inode map control structure.
 *	agno	- allocation group.
 *	ip	- pointer to new inode to be filled in on successful return
 *		  with the disk inode number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diAllocIno(struct inomap * imap, int agno, struct inode *ip)
{
	int iagno, ino, rc, rem, extno, sword;
	struct metapage *mp;
	struct iag *iagp;

	/* check if there are iags on the ag's free inode list.
	 */
	if ((iagno = imap->im_agctl[agno].inofree) < 0)
		return -ENOSPC;

	/* obtain read lock on imap inode */
	IREAD_LOCK(imap->im_ipimap, RDWRLOCK_IMAP);

	/* read the iag at the head of the list.
	 */
	if ((rc = diIAGRead(imap, iagno, &mp))) {
		IREAD_UNLOCK(imap->im_ipimap);
		return (rc);
	}
	iagp = (struct iag *) mp->data;

	/* better be free inodes in this iag if it is on the
	 * list.
	 */
	if (!iagp->nfreeinos) {
		IREAD_UNLOCK(imap->im_ipimap);
		release_metapage(mp);
		jfs_error(ip->i_sb,
			  "diAllocIno: nfreeinos = 0, but iag on freelist");
		return -EIO;
	}

	/* scan the free inode summary map to find an extent
	 * with free inodes.
	 */
	for (sword = 0;; sword++) {
		if (sword >= SMAPSZ) {
			IREAD_UNLOCK(imap->im_ipimap);
			release_metapage(mp);
			jfs_error(ip->i_sb,
				  "diAllocIno: free inode not found in summary map");
			return -EIO;
		}

		if (~iagp->inosmap[sword])
			break;
	}

	/* found a extent with free inodes. determine
	 * the extent number.
	 */
	rem = diFindFree(le32_to_cpu(iagp->inosmap[sword]), 0);
	if (rem >= EXTSPERSUM) {
		IREAD_UNLOCK(imap->im_ipimap);
		release_metapage(mp);
		jfs_error(ip->i_sb, "diAllocIno: no free extent found");
		return -EIO;
	}
	extno = (sword << L2EXTSPERSUM) + rem;

	/* find the first free inode in the extent.
	 */
	rem = diFindFree(le32_to_cpu(iagp->wmap[extno]), 0);
	if (rem >= INOSPEREXT) {
		IREAD_UNLOCK(imap->im_ipimap);
		release_metapage(mp);
		jfs_error(ip->i_sb, "diAllocIno: free inode not found");
		return -EIO;
	}

	/* compute the inode number within the iag.
	 */
	ino = (extno << L2INOSPEREXT) + rem;

	/* allocate the inode.
	 */
	rc = diAllocBit(imap, iagp, ino);
	IREAD_UNLOCK(imap->im_ipimap);
	if (rc) {
		release_metapage(mp);
		return (rc);
	}

	/* set the results of the allocation and write the iag.
	 */
	diInitInode(ip, iagno, ino, extno, iagp);
	write_metapage(mp);

	return (0);
}


/*
 * NAME:	diAllocExt(imap,agno,ip)
 *
 * FUNCTION:	add a new extent of free inodes to an iag, allocating
 *		an inode from this extent to satisfy the current allocation
 *		request.
 *
 *		this routine first tries to find an existing iag with free
 *		extents through the ag free extent list.  if list is not
 *		empty, the head of the list will be selected as the home
 *		of the new extent of free inodes.  otherwise (the list is
 *		empty), a new iag will be allocated for the ag to contain
 *		the extent.
 *
 *		once an iag has been selected, the free extent summary map
 *		is used to locate a free extent within the iag and diNewExt()
 *		is called to initialize the extent, with initialization
 *		including the allocation of the first inode of the extent
 *		for the purpose of satisfying this request.
 *
 * PARAMETERS:
 *	imap	- pointer to inode map control structure.
 *	agno	- allocation group number.
 *	ip	- pointer to new inode to be filled in on successful return
 *		  with the disk inode number allocated, its extent address
 *		  and the start of the ag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diAllocExt(struct inomap * imap, int agno, struct inode *ip)
{
	int rem, iagno, sword, extno, rc;
	struct metapage *mp;
	struct iag *iagp;

	/* check if the ag has any iags with free extents.  if not,
	 * allocate a new iag for the ag.
	 */
	if ((iagno = imap->im_agctl[agno].extfree) < 0) {
		/* If successful, diNewIAG will obtain the read lock on the
		 * imap inode.
		 */
		if ((rc = diNewIAG(imap, &iagno, agno, &mp))) {
			return (rc);
		}
		iagp = (struct iag *) mp->data;

		/* set the ag number if this a brand new iag
		 */
		iagp->agstart =
		    cpu_to_le64(AGTOBLK(agno, imap->im_ipimap));
	} else {
		/* read the iag.
		 */
		IREAD_LOCK(imap->im_ipimap, RDWRLOCK_IMAP);
		if ((rc = diIAGRead(imap, iagno, &mp))) {
			IREAD_UNLOCK(imap->im_ipimap);
			jfs_error(ip->i_sb, "diAllocExt: error reading iag");
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
			jfs_error(ip->i_sb,
				  "diAllocExt: free ext summary map not found");
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
		jfs_error(ip->i_sb, "diAllocExt: free extent not found");
		return -EIO;
	}
	extno = (sword << L2EXTSPERSUM) + rem;

	/* initialize the new extent.
	 */
	rc = diNewExt(imap, iagp, extno);
	IREAD_UNLOCK(imap->im_ipimap);
	if (rc) {
		/* something bad happened.  if a new iag was allocated,
		 * place it back on the inode map's iag free list, and
		 * clear the ag number information.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG)) {
			IAGFREE_LOCK(imap);
			iagp->iagfree = cpu_to_le32(imap->im_freeiag);
			imap->im_freeiag = iagno;
			IAGFREE_UNLOCK(imap);
		}
		write_metapage(mp);
		return (rc);
	}

	/* set the results of the allocation and write the iag.
	 */
	diInitInode(ip, iagno, extno << L2INOSPEREXT, extno, iagp);

	write_metapage(mp);

	return (0);
}


/*
 * NAME:	diAllocBit(imap,iagp,ino)
 *
 * FUNCTION:	allocate a backed inode from an iag.
 *
 *		this routine performs the mechanics of allocating a
 *		specified inode from a backed extent.
 *
 *		if the inode to be allocated represents the last free
 *		inode within the iag, the iag will be removed from the
 *		ag free inode list.
 *
 *		a careful update approach is used to provide consistency
 *		in the face of updates to multiple buffers.  under this
 *		approach, all required buffers are obtained before making
 *		any updates and are held all are updates are complete.
 *
 * PRE CONDITION: Already have buffer lock on iagp.  Already have AG lock on
 *	this AG.  Must have read lock on imap inode.
 *
 * PARAMETERS:
 *	imap	- pointer to inode map control structure.
 *	iagp	- pointer to iag.
 *	ino	- inode number to be allocated within the iag.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diAllocBit(struct inomap * imap, struct iag * iagp, int ino)
{
	int extno, bitno, agno, sword, rc;
	struct metapage *amp = NULL, *bmp = NULL;
	struct iag *aiagp = NULL, *biagp = NULL;
	u32 mask;

	/* check if this is the last free inode within the iag.
	 * if so, it will have to be removed from the ag free
	 * inode list, so get the iags preceding and following
	 * it on the list.
	 */
	if (iagp->nfreeinos == cpu_to_le32(1)) {
		if ((int) le32_to_cpu(iagp->inofreefwd) >= 0) {
			if ((rc =
			     diIAGRead(imap, le32_to_cpu(iagp->inofreefwd),
				       &amp)))
				return (rc);
			aiagp = (struct iag *) amp->data;
		}

		if ((int) le32_to_cpu(iagp->inofreeback) >= 0) {
			if ((rc =
			     diIAGRead(imap,
				       le32_to_cpu(iagp->inofreeback),
				       &bmp))) {
				if (amp)
					release_metapage(amp);
				return (rc);
			}
			biagp = (struct iag *) bmp->data;
		}
	}

	/* get the ag number, extent number, inode number within
	 * the extent.
	 */
	agno = BLKTOAG(le64_to_cpu(iagp->agstart), JFS_SBI(imap->im_ipimap->i_sb));
	extno = ino >> L2INOSPEREXT;
	bitno = ino & (INOSPEREXT - 1);

	/* compute the mask for setting the map.
	 */
	mask = HIGHORDER >> bitno;

	/* the inode should be free and backed.
	 */
	if (((le32_to_cpu(iagp->pmap[extno]) & mask) != 0) ||
	    ((le32_to_cpu(iagp->wmap[extno]) & mask) != 0) ||
	    (addressPXD(&iagp->inoext[extno]) == 0)) {
		if (amp)
			release_metapage(amp);
		if (bmp)
			release_metapage(bmp);

		jfs_error(imap->im_ipimap->i_sb,
			  "diAllocBit: iag inconsistent");
		return -EIO;
	}

	/* mark the inode as allocated in the working map.
	 */
	iagp->wmap[extno] |= cpu_to_le32(mask);

	/* check if all inodes within the extent are now
	 * allocated.  if so, update the free inode summary
	 * map to reflect this.
	 */
	if (iagp->wmap[extno] == cpu_to_le32(ONES)) {
		sword = extno >> L2EXTSPERSUM;
		bitno = extno & (EXTSPERSUM - 1);
		iagp->inosmap[sword] |= cpu_to_le32(HIGHORDER >> bitno);
	}

	/* if this was the last free inode in the iag, remove the
	 * iag from the ag free inode list.
	 */
	if (iagp->nfreeinos == cpu_to_le32(1)) {
		if (amp) {
			aiagp->inofreeback = iagp->inofreeback;
			write_metapage(amp);
		}

		if (bmp) {
			biagp->inofreefwd = iagp->inofreefwd;
			write_metapage(bmp);
		} else {
			imap->im_agctl[agno].inofree =
			    le32_to_cpu(iagp->inofreefwd);
		}
		iagp->inofreefwd = iagp->inofreeback = cpu_to_le32(-1);
	}

	/* update the free inode count at the iag, ag, inode
	 * map levels.
	 */
	le32_add_cpu(&iagp->nfreeinos, -1);
	imap->im_agctl[agno].numfree -= 1;
	atomic_dec(&imap->im_numfree);

	return (0);
}


/*
 * NAME:	diNewExt(imap,iagp,extno)
 *
 * FUNCTION:	initialize a new extent of inodes for an iag, allocating
 *		the first inode of the extent for use for the current
 *		allocation request.
 *
 *		disk resources are allocated for the new extent of inodes
 *		and the inodes themselves are initialized to reflect their
 *		existence within the extent (i.e. their inode numbers and
 *		inode extent addresses are set) and their initial state
 *		(mode and link count are set to zero).
 *
 *		if the iag is new, it is not yet on an ag extent free list
 *		but will now be placed on this list.
 *
 *		if the allocation of the new extent causes the iag to
 *		have no free extent, the iag will be removed from the
 *		ag extent free list.
 *
 *		if the iag has no free backed inodes, it will be placed
 *		on the ag free inode list, since the addition of the new
 *		extent will now cause it to have free inodes.
 *
 *		a careful update approach is used to provide consistency
 *		(i.e. list consistency) in the face of updates to multiple
 *		buffers.  under this approach, all required buffers are
 *		obtained before making any updates and are held until all
 *		updates are complete.
 *
 * PRE CONDITION: Already have buffer lock on iagp.  Already have AG lock on
 *	this AG.  Must have read lock on imap inode.
 *
 * PARAMETERS:
 *	imap	- pointer to inode map control structure.
 *	iagp	- pointer to iag.
 *	extno	- extent number.
 *
 * RETURN VALUES:
 *	0	- success.
 *	-ENOSPC	- insufficient disk resources.
 *	-EIO	- i/o error.
 */
static int diNewExt(struct inomap * imap, struct iag * iagp, int extno)
{
	int agno, iagno, fwd, back, freei = 0, sword, rc;
	struct iag *aiagp = NULL, *biagp = NULL, *ciagp = NULL;
	struct metapage *amp, *bmp, *cmp, *dmp;
	struct inode *ipimap;
	s64 blkno, hint;
	int i, j;
	u32 mask;
	ino_t ino;
	struct dinode *dp;
	struct jfs_sb_info *sbi;

	/* better have free extents.
	 */
	if (!iagp->nfreeexts) {
		jfs_error(imap->im_ipimap->i_sb, "diNewExt: no free extents");
		return -EIO;
	}

	/* get the inode map inode.
	 */
	ipimap = imap->im_ipimap;
	sbi = JFS_SBI(ipimap->i_sb);

	amp = bmp = cmp = NULL;

	/* get the ag and iag numbers for this iag.
	 */
	agno = BLKTOAG(le64_to_cpu(iagp->agstart), sbi);
	iagno = le32_to_cpu(iagp->iagnum);

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
			if ((fwd = imap->im_agctl[agno].extfree) >= 0) {
				if ((rc = diIAGRead(imap, fwd, &amp)))
					goto error_out;
				aiagp = (struct iag *) amp->data;
			}
		}
	}

	/* check if the iag has no free inodes.  if so, the iag
	 * will have to be added to the ag free inode list, so get
	 * the iag at the head of the list in preparation for
	 * adding this iag to this list.  in doing this, we must
	 * check if we already have the iag at the head of
	 * the list in hand.
	 */
	if (iagp->nfreeinos == 0) {
		freei = imap->im_agctl[agno].inofree;

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
					  "diNewExt: ciagp == NULL");
				rc = -EIO;
				goto error_out;
			}
		}
	}

	/* allocate disk space for the inode extent.
	 */
	if ((extno == 0) || (addressPXD(&iagp->inoext[extno - 1]) == 0))
		hint = ((s64) agno << sbi->bmap->db_agl2size) - 1;
	else
		hint = addressPXD(&iagp->inoext[extno - 1]) +
		    lengthPXD(&iagp->inoext[extno - 1]) - 1;

	if ((rc = dbAlloc(ipimap, hint, (s64) imap->im_nbperiext, &blkno)))
		goto error_out;

	/* compute the inode number of the first inode within the
	 * extent.
	 */
	ino = (iagno << L2INOSPERIAG) + (extno << L2INOSPEREXT);

	/* initialize the inodes within the newly allocated extent a
	 * page at a time.
	 */
	for (i = 0; i < imap->im_nbperiext; i += sbi->nbperpage) {
		/* get a buffer for this page of disk inodes.
		 */
		dmp = get_metapage(ipimap, blkno + i, PSIZE, 1);
		if (dmp == NULL) {
			rc = -EIO;
			goto error_out;
		}
		dp = (struct dinode *) dmp->data;

		/* initialize the inode number, mode, link count and
		 * inode extent address.
		 */
		for (j = 0; j < INOSPERPAGE; j++, dp++, ino++) {
			dp->di_inostamp = cpu_to_le32(sbi->inostamp);
			dp->di_number = cpu_to_le32(ino);
			dp->di_fileset = cpu_to_le32(FILESYSTEM_I);
			dp->di_mode = 0;
			dp->di_nlink = 0;
			PXDaddress(&(dp->di_ixpxd), blkno);
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
			imap->im_agctl[agno].extfree =
			    le32_to_cpu(iagp->extfreefwd);

		iagp->extfreefwd = iagp->extfreeback = cpu_to_le32(-1);
	} else {
		/* if the iag has all free extents (newly allocated iag),
		 * add the iag to the ag free extent list.
		 */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG)) {
			if (fwd >= 0)
				aiagp->extfreeback = cpu_to_le32(iagno);

			iagp->extfreefwd = cpu_to_le32(fwd);
			iagp->extfreeback = cpu_to_le32(-1);
			imap->im_agctl[agno].extfree = iagno;
		}
	}

	/* if the iag has no free inodes, add the iag to the
	 * ag free inode list.
	 */
	if (iagp->nfreeinos == 0) {
		if (freei >= 0)
			ciagp->inofreeback = cpu_to_le32(iagno);

		iagp->inofreefwd =
		    cpu_to_le32(imap->im_agctl[agno].inofree);
		iagp->inofreeback = cpu_to_le32(-1);
		imap->im_agctl[agno].inofree = iagno;
	}

	/* initialize the extent descriptor of the extent. */
	PXDlength(&iagp->inoext[extno], imap->im_nbperiext);
	PXDaddress(&iagp->inoext[extno], blkno);

	/* initialize the working and persistent map of the extent.
	 * the working map will be initialized such that
	 * it indicates the first inode of the extent is allocated.
	 */
	iagp->wmap[extno] = cpu_to_le32(HIGHORDER);
	iagp->pmap[extno] = 0;

	/* update the free inode and free extent summary maps
	 * for the extent to indicate the extent has free inodes
	 * and no longer represents a free extent.
	 */
	sword = extno >> L2EXTSPERSUM;
	mask = HIGHORDER >> (extno & (EXTSPERSUM - 1));
	iagp->extsmap[sword] |= cpu_to_le32(mask);
	iagp->inosmap[sword] &= cpu_to_le32(~mask);

	/* update the free inode and free extent counts for the
	 * iag.
	 */
	le32_add_cpu(&iagp->nfreeinos, (INOSPEREXT - 1));
	le32_add_cpu(&iagp->nfreeexts, -1);

	/* update the free and backed inode counts for the ag.
	 */
	imap->im_agctl[agno].numfree += (INOSPEREXT - 1);
	imap->im_agctl[agno].numinos += INOSPEREXT;

	/* update the free and backed inode counts for the inode map.
	 */
	atomic_add(INOSPEREXT - 1, &imap->im_numfree);
	atomic_add(INOSPEREXT, &imap->im_numinos);

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
 * NAME:	diNewIAG(imap,iagnop,agno)
 *
 * FUNCTION:	allocate a new iag for an allocation group.
 *
 *		first tries to allocate the iag from the inode map
 *		iagfree list:
 *		if the list has free iags, the head of the list is removed
 *		and returned to satisfy the request.
 *		if the inode map's iag free list is empty, the inode map
 *		is extended to hold a new iag. this new iag is initialized
 *		and returned to satisfy the request.
 *
 * PARAMETERS:
 *	imap	- pointer to inode map control structure.
 *	iagnop	- pointer to an iag number set with the number of the
 *		  newly allocated iag upon successful return.
 *	agno	- allocation group number.
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
 * note: new iag transaction:
 * . synchronously write iag;
 * . write log of xtree and inode of imap;
 * . commit;
 * . synchronous write of xtree (right to left, bottom to top);
 * . at start of logredo(): init in-memory imap with one additional iag page;
 * . at end of logredo(): re-read imap inode to determine
 *   new imap size;
 */
static int
diNewIAG(struct inomap * imap, int *iagnop, int agno, struct metapage ** mpp)
{
	int rc;
	int iagno, i, xlen;
	struct inode *ipimap;
	struct super_block *sb;
	struct jfs_sb_info *sbi;
	struct metapage *mp;
	struct iag *iagp;
	s64 xaddr = 0;
	s64 blkno;
	tid_t tid;
	struct inode *iplist[1];

	/* pick up pointers to the inode map and mount inodes */
	ipimap = imap->im_ipimap;
	sb = ipimap->i_sb;
	sbi = JFS_SBI(sb);

	/* acquire the free iag lock */
	IAGFREE_LOCK(imap);

	/* if there are any iags on the inode map free iag list,
	 * allocate the iag from the head of the list.
	 */
	if (imap->im_freeiag >= 0) {
		/* pick up the iag number at the head of the list */
		iagno = imap->im_freeiag;

		/* determine the logical block number of the iag */
		blkno = IAGTOLBLK(iagno, sbi->l2nbperpage);
	} else {
		/* no free iags. the inode map will have to be extented
		 * to include a new iag.
		 */

		/* acquire inode map lock */
		IWRITE_LOCK(ipimap, RDWRLOCK_IMAP);

		if (ipimap->i_size >> L2PSIZE != imap->im_nextiag + 1) {
			IWRITE_UNLOCK(ipimap);
			IAGFREE_UNLOCK(imap);
			jfs_error(imap->im_ipimap->i_sb,
				  "diNewIAG: ipimap->i_size is wrong");
			return -EIO;
		}


		/* get the next available iag number */
		iagno = imap->im_nextiag;

		/* make sure that we have not exceeded the maximum inode
		 * number limit.
		 */
		if (iagno > (MAXIAGS - 1)) {
			/* release the inode map lock */
			IWRITE_UNLOCK(ipimap);

			rc = -ENOSPC;
			goto out;
		}

		/*
		 * synchronously append new iag page.
		 */
		/* determine the logical address of iag page to append */
		blkno = IAGTOLBLK(iagno, sbi->l2nbperpage);

		/* Allocate extent for new iag page */
		xlen = sbi->nbperpage;
		if ((rc = dbAlloc(ipimap, 0, (s64) xlen, &xaddr))) {
			/* release the inode map lock */
			IWRITE_UNLOCK(ipimap);

			goto out;
		}

		/*
		 * start transaction of update of the inode map
		 * addressing structure pointing to the new iag page;
		 */
		tid = txBegin(sb, COMMIT_FORCE);
		mutex_lock(&JFS_IP(ipimap)->commit_mutex);

		/* update the inode map addressing structure to point to it */
		if ((rc =
		     xtInsert(tid, ipimap, 0, blkno, xlen, &xaddr, 0))) {
			txEnd(tid);
			mutex_unlock(&JFS_IP(ipimap)->commit_mutex);
			/* Free the blocks allocated for the iag since it was
			 * not successfully added to the inode map
			 */
			dbFree(ipimap, xaddr, (s64) xlen);

			/* release the inode map lock */
			IWRITE_UNLOCK(ipimap);

			goto out;
		}

		/* update the inode map's inode to reflect the extension */
		ipimap->i_size += PSIZE;
		inode_add_bytes(ipimap, PSIZE);

		/* assign a buffer for the page */
		mp = get_metapage(ipimap, blkno, PSIZE, 0);
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

			/* release the inode map lock */
			IWRITE_UNLOCK(ipimap);

			rc = -EIO;
			goto out;
		}
		iagp = (struct iag *) mp->data;

		/* init the iag */
		memset(iagp, 0, sizeof(struct iag));
		iagp->iagnum = cpu_to_le32(iagno);
		iagp->inofreefwd = iagp->inofreeback = cpu_to_le32(-1);
		iagp->extfreefwd = iagp->extfreeback = cpu_to_le32(-1);
		iagp->iagfree = cpu_to_le32(-1);
		iagp->nfreeinos = 0;
		iagp->nfreeexts = cpu_to_le32(EXTSPERIAG);

		/* initialize the free inode summary map (free extent
		 * summary map initialization handled by bzero).
		 */
		for (i = 0; i < SMAPSZ; i++)
			iagp->inosmap[i] = cpu_to_le32(ONES);

		/*
		 * Write and sync the metapage
		 */
		flush_metapage(mp);

		/*
		 * txCommit(COMMIT_FORCE) will synchronously write address
		 * index pages and inode after commit in careful update order
		 * of address index pages (right to left, bottom up);
		 */
		iplist[0] = ipimap;
		rc = txCommit(tid, 1, &iplist[0], COMMIT_FORCE);

		txEnd(tid);
		mutex_unlock(&JFS_IP(ipimap)->commit_mutex);

		duplicateIXtree(sb, blkno, xlen, &xaddr);

		/* update the next available iag number */
		imap->im_nextiag += 1;

		/* Add the iag to the iag free list so we don't lose the iag
		 * if a failure happens now.
		 */
		imap->im_freeiag = iagno;

		/* Until we have logredo working, we want the imap inode &
		 * control page to be up to date.
		 */
		diSync(ipimap);

		/* release the inode map lock */
		IWRITE_UNLOCK(ipimap);
	}

	/* obtain read lock on map */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);

	/* read the iag */
	if ((rc = diIAGRead(imap, iagno, &mp))) {
		IREAD_UNLOCK(ipimap);
		rc = -EIO;
		goto out;
	}
	iagp = (struct iag *) mp->data;

	/* remove the iag from the iag free list */
	imap->im_freeiag = le32_to_cpu(iagp->iagfree);
	iagp->iagfree = cpu_to_le32(-1);

	/* set the return iag number and buffer pointer */
	*iagnop = iagno;
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
 *		or aggregate inode map.
 *
 * PARAMETERS:
 *	imap	- pointer to inode map control structure.
 *	iagno	- iag number.
 *	bpp	- point to buffer pointer to be filled in on successful
 *		  exit.
 *
 * SERIALIZATION:
 *	must have read lock on imap inode
 *	(When called by diExtendFS, the filesystem is quiesced, therefore
 *	 the read lock is unnecessary.)
 *
 * RETURN VALUES:
 *	0	- success.
 *	-EIO	- i/o error.
 */
static int diIAGRead(struct inomap * imap, int iagno, struct metapage ** mpp)
{
	struct inode *ipimap = imap->im_ipimap;
	s64 blkno;

	/* compute the logical block number of the iag. */
	blkno = IAGTOLBLK(iagno, JFS_SBI(ipimap->i_sb)->l2nbperpage);

	/* read the iag. */
	*mpp = read_metapage(ipimap, blkno, PSIZE, 0);
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
 *	no free bits were found.
 */
static int diFindFree(u32 word, int start)
{
	int bitno;
	assert(start < 32);
	/* scan the word for the first free bit. */
	for (word <<= start, bitno = start; bitno < 32;
	     bitno++, word <<= 1) {
		if ((word & HIGHORDER) == 0)
			break;
	}
	return (bitno);
}

/*
 * NAME:	diUpdatePMap()
 *
 * FUNCTION: Update the persistent map in an IAG for the allocation or
 *	freeing of the specified inode.
 *
 * PRE CONDITIONS: Working map has already been updated for allocate.
 *
 * PARAMETERS:
 *	ipimap	- Incore inode map inode
 *	inum	- Number of inode to mark in permanent map
 *	is_free	- If 'true' indicates inode should be marked freed, otherwise
 *		  indicates inode should be marked allocated.
 *
 * RETURN VALUES:
 *		0 for success
 */
int
diUpdatePMap(struct inode *ipimap,
	     unsigned long inum, bool is_free, struct tblock * tblk)
{
	int rc;
	struct iag *iagp;
	struct metapage *mp;
	int iagno, ino, extno, bitno;
	struct inomap *imap;
	u32 mask;
	struct jfs_log *log;
	int lsn, difft, diffp;
	unsigned long flags;

	imap = JFS_IP(ipimap)->i_imap;
	/* get the iag number containing the inode */
	iagno = INOTOIAG(inum);
	/* make sure that the iag is contained within the map */
	if (iagno >= imap->im_nextiag) {
		jfs_error(ipimap->i_sb,
			  "diUpdatePMap: the iag is outside the map");
		return -EIO;
	}
	/* read the iag */
	IREAD_LOCK(ipimap, RDWRLOCK_IMAP);
	rc = diIAGRead(imap, iagno, &mp);
	IREAD_UNLOCK(ipimap);
	if (rc)
		return (rc);
	metapage_wait_for_io(mp);
	iagp = (struct iag *) mp->data;
	/* get the inode number and extent number of the inode within
	 * the iag and the inode number within the extent.
	 */
	ino = inum & (INOSPERIAG - 1);
	extno = ino >> L2INOSPEREXT;
	bitno = ino & (INOSPEREXT - 1);
	mask = HIGHORDER >> bitno;
	/*
	 * mark the inode free in persistent map:
	 */
	if (is_free) {
		/* The inode should have been allocated both in working
		 * map and in persistent map;
		 * the inode will be freed from working map at the release
		 * of last reference release;
		 */
		if (!(le32_to_cpu(iagp->wmap[extno]) & mask)) {
			jfs_error(ipimap->i_sb,
				  "diUpdatePMap: inode %ld not marked as "
				  "allocated in wmap!", inum);
		}
		if (!(le32_to_cpu(iagp->pmap[extno]) & mask)) {
			jfs_error(ipimap->i_sb,
				  "diUpdatePMap: inode %ld not marked as "
				  "allocated in pmap!", inum);
		}
		/* update the bitmap for the extent of the freed inode */
		iagp->pmap[extno] &= cpu_to_le32(~mask);
	}
	/*
	 * mark the inode allocated in persistent map:
	 */
	else {
		/* The inode should be already allocated in the working map
		 * and should be free in persistent map;
		 */
		if (!(le32_to_cpu(iagp->wmap[extno]) & mask)) {
			release_metapage(mp);
			jfs_error(ipimap->i_sb,
				  "diUpdatePMap: the inode is not allocated in "
				  "the working map");
			return -EIO;
		}
		if ((le32_to_cpu(iagp->pmap[extno]) & mask) != 0) {
			release_metapage(mp);
			jfs_error(ipimap->i_sb,
				  "diUpdatePMap: the inode is not free in the "
				  "persistent map");
			return -EIO;
		}
		/* update the bitmap for the extent of the allocated inode */
		iagp->pmap[extno] |= cpu_to_le32(mask);
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
 * note: AG size has been increased s.t. each k old contiguous AGs are
 * coalesced into a new AG;
 */
int diExtendFS(struct inode *ipimap, struct inode *ipbmap)
{
	int rc, rcx = 0;
	struct inomap *imap = JFS_IP(ipimap)->i_imap;
	struct iag *iagp = NULL, *hiagp = NULL;
	struct bmap *mp = JFS_SBI(ipbmap->i_sb)->bmap;
	struct metapage *bp, *hbp;
	int i, n, head;
	int numinos, xnuminos = 0, xnumfree = 0;
	s64 agstart;

	jfs_info("diExtendFS: nextiag:%d numinos:%d numfree:%d",
		   imap->im_nextiag, atomic_read(&imap->im_numinos),
		   atomic_read(&imap->im_numfree));

	/*
	 *	reconstruct imap
	 *
	 * coalesce contiguous k (newAGSize/oldAGSize) AGs;
	 * i.e., (AGi, ..., AGj) where i = k*n and j = k*(n+1) - 1 to AGn;
	 * note: new AG size = old AG size * (2**x).
	 */

	/* init per AG control information im_agctl[] */
	for (i = 0; i < MAXAG; i++) {
		imap->im_agctl[i].inofree = -1;
		imap->im_agctl[i].extfree = -1;
		imap->im_agctl[i].numinos = 0;	/* number of backed inodes */
		imap->im_agctl[i].numfree = 0;	/* number of free backed inodes */
	}

	/*
	 *	process each iag page of the map.
	 *
	 * rebuild AG Free Inode List, AG Free Inode Extent List;
	 */
	for (i = 0; i < imap->im_nextiag; i++) {
		if ((rc = diIAGRead(imap, i, &bp))) {
			rcx = rc;
			continue;
		}
		iagp = (struct iag *) bp->data;
		if (le32_to_cpu(iagp->iagnum) != i) {
			release_metapage(bp);
			jfs_error(ipimap->i_sb,
				  "diExtendFs: unexpected value of iagnum");
			return -EIO;
		}

		/* leave free iag in the free iag list */
		if (iagp->nfreeexts == cpu_to_le32(EXTSPERIAG)) {
			release_metapage(bp);
			continue;
		}

		/* agstart that computes to the same ag is treated as same; */
		agstart = le64_to_cpu(iagp->agstart);
		/* iagp->agstart = agstart & ~(mp->db_agsize - 1); */
		n = agstart >> mp->db_agl2size;

		/* compute backed inodes */
		numinos = (EXTSPERIAG - le32_to_cpu(iagp->nfreeexts))
		    << L2INOSPEREXT;
		if (numinos > 0) {
			/* merge AG backed inodes */
			imap->im_agctl[n].numinos += numinos;
			xnuminos += numinos;
		}

		/* if any backed free inodes, insert at AG free inode list */
		if ((int) le32_to_cpu(iagp->nfreeinos) > 0) {
			if ((head = imap->im_agctl[n].inofree) == -1) {
				iagp->inofreefwd = cpu_to_le32(-1);
				iagp->inofreeback = cpu_to_le32(-1);
			} else {
				if ((rc = diIAGRead(imap, head, &hbp))) {
					rcx = rc;
					goto nextiag;
				}
				hiagp = (struct iag *) hbp->data;
				hiagp->inofreeback = iagp->iagnum;
				iagp->inofreefwd = cpu_to_le32(head);
				iagp->inofreeback = cpu_to_le32(-1);
				write_metapage(hbp);
			}

			imap->im_agctl[n].inofree =
			    le32_to_cpu(iagp->iagnum);

			/* merge AG backed free inodes */
			imap->im_agctl[n].numfree +=
			    le32_to_cpu(iagp->nfreeinos);
			xnumfree += le32_to_cpu(iagp->nfreeinos);
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

	if (xnuminos != atomic_read(&imap->im_numinos) ||
	    xnumfree != atomic_read(&imap->im_numfree)) {
		jfs_error(ipimap->i_sb,
			  "diExtendFs: numinos or numfree incorrect");
		return -EIO;
	}

	return rcx;
}


/*
 *	duplicateIXtree()
 *
 * serialization: IWRITE_LOCK held on entry/exit
 *
 * note: shadow page with regular inode (rel.2);
 */
static void duplicateIXtree(struct super_block *sb, s64 blkno,
			    int xlen, s64 *xaddr)
{
	struct jfs_superblock *j_sb;
	struct buffer_head *bh;
	struct inode *ip;
	tid_t tid;

	/* if AIT2 ipmap2 is bad, do not try to update it */
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
	/* update the inode map addressing structure to point to it */
	if (xtInsert(tid, ip, 0, blkno, xlen, xaddr, 0)) {
		JFS_SBI(sb)->mntflag |= JFS_BAD_SAIT;
		txAbort(tid, 1);
		goto cleanup;

	}
	/* update the inode map's inode to reflect the extension */
	ip->i_size += PSIZE;
	inode_add_bytes(ip, PSIZE);
	txCommit(tid, 1, &ip, COMMIT_FORCE);
      cleanup:
	txEnd(tid);
	diFreeSpecial(ip);
}

/*
 * NAME:	copy_from_dinode()
 *
 * FUNCTION:	Copies inode info from disk inode to in-memory inode
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOMEM	- insufficient memory
 */
static int copy_from_dinode(struct dinode * dip, struct inode *ip)
{
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);

	jfs_ip->fileset = le32_to_cpu(dip->di_fileset);
	jfs_ip->mode2 = le32_to_cpu(dip->di_mode);
	jfs_set_inode_flags(ip);

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
	ip->i_nlink = le32_to_cpu(dip->di_nlink);

	jfs_ip->saved_uid = le32_to_cpu(dip->di_uid);
	if (sbi->uid == -1)
		ip->i_uid = jfs_ip->saved_uid;
	else {
		ip->i_uid = sbi->uid;
	}

	jfs_ip->saved_gid = le32_to_cpu(dip->di_gid);
	if (sbi->gid == -1)
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
 * NAME:	copy_to_dinode()
 *
 * FUNCTION:	Copies inode info from in-memory inode to disk inode
 */
static void copy_to_dinode(struct dinode * dip, struct inode *ip)
{
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);

	dip->di_fileset = cpu_to_le32(jfs_ip->fileset);
	dip->di_inostamp = cpu_to_le32(sbi->inostamp);
	dip->di_number = cpu_to_le32(ip->i_ino);
	dip->di_gen = cpu_to_le32(ip->i_generation);
	dip->di_size = cpu_to_le64(ip->i_size);
	dip->di_nblocks = cpu_to_le64(PBLK2LBLK(ip->i_sb, ip->i_blocks));
	dip->di_nlink = cpu_to_le32(ip->i_nlink);
	if (sbi->uid == -1)
		dip->di_uid = cpu_to_le32(ip->i_uid);
	else
		dip->di_uid = cpu_to_le32(jfs_ip->saved_uid);
	if (sbi->gid == -1)
		dip->di_gid = cpu_to_le32(ip->i_gid);
	else
		dip->di_gid = cpu_to_le32(jfs_ip->saved_gid);
	jfs_get_inode_flags(jfs_ip);
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
