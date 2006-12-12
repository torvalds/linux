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

#include <linux/fs.h>
#include <linux/quotaops.h>
#include "jfs_incore.h"
#include "jfs_inode.h"
#include "jfs_superblock.h"
#include "jfs_dmap.h"
#include "jfs_extent.h"
#include "jfs_debug.h"

/*
 * forward references
 */
static int extBalloc(struct inode *, s64, s64 *, s64 *);
#ifdef _NOTYET
static int extBrealloc(struct inode *, s64, s64, s64 *, s64 *);
#endif
static s64 extRoundDown(s64 nb);

#define DPD(a)          (printk("(a): %d\n",(a)))
#define DPC(a)          (printk("(a): %c\n",(a)))
#define DPL1(a)					\
{						\
	if ((a) >> 32)				\
		printk("(a): %x%08x  ",(a));	\
	else					\
		printk("(a): %x  ",(a) << 32);	\
}
#define DPL(a)					\
{						\
	if ((a) >> 32)				\
		printk("(a): %x%08x\n",(a));	\
	else					\
		printk("(a): %x\n",(a) << 32);	\
}

#define DPD1(a)         (printk("(a): %d  ",(a)))
#define DPX(a)          (printk("(a): %08x\n",(a)))
#define DPX1(a)         (printk("(a): %08x  ",(a)))
#define DPS(a)          (printk("%s\n",(a)))
#define DPE(a)          (printk("\nENTERING: %s\n",(a)))
#define DPE1(a)          (printk("\nENTERING: %s",(a)))
#define DPS1(a)         (printk("  %s  ",(a)))


/*
 * NAME:	extAlloc()
 *
 * FUNCTION:    allocate an extent for a specified page range within a
 *		file.
 *
 * PARAMETERS:
 *	ip	- the inode of the file.
 *	xlen	- requested extent length.
 *	pno	- the starting page number with the file.
 *	xp	- pointer to an xad.  on entry, xad describes an
 *		  extent that is used as an allocation hint if the
 *		  xaddr of the xad is non-zero.  on successful exit,
 *		  the xad describes the newly allocated extent.
 *	abnr	- bool indicating whether the newly allocated extent
 *		  should be marked as allocated but not recorded.
 *
 * RETURN VALUES:
 *      0       - success
 *      -EIO	- i/o error.
 *      -ENOSPC	- insufficient disk resources.
 */
int
extAlloc(struct inode *ip, s64 xlen, s64 pno, xad_t * xp, bool abnr)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	s64 nxlen, nxaddr, xoff, hint, xaddr = 0;
	int rc;
	int xflag;

	/* This blocks if we are low on resources */
	txBeginAnon(ip->i_sb);

	/* Avoid race with jfs_commit_inode() */
	mutex_lock(&JFS_IP(ip)->commit_mutex);

	/* validate extent length */
	if (xlen > MAXXLEN)
		xlen = MAXXLEN;

	/* get the page's starting extent offset */
	xoff = pno << sbi->l2nbperpage;

	/* check if an allocation hint was provided */
	if ((hint = addressXAD(xp))) {
		/* get the size of the extent described by the hint */
		nxlen = lengthXAD(xp);

		/* check if the hint is for the portion of the file
		 * immediately previous to the current allocation
		 * request and if hint extent has the same abnr
		 * value as the current request.  if so, we can
		 * extend the hint extent to include the current
		 * extent if we can allocate the blocks immediately
		 * following the hint extent.
		 */
		if (offsetXAD(xp) + nxlen == xoff &&
		    abnr == ((xp->flag & XAD_NOTRECORDED) ? true : false))
			xaddr = hint + nxlen;

		/* adjust the hint to the last block of the extent */
		hint += (nxlen - 1);
	}

	/* allocate the disk blocks for the extent.  initially, extBalloc()
	 * will try to allocate disk blocks for the requested size (xlen).
	 * if this fails (xlen contiguous free blocks not avaliable), it'll
	 * try to allocate a smaller number of blocks (producing a smaller
	 * extent), with this smaller number of blocks consisting of the
	 * requested number of blocks rounded down to the next smaller
	 * power of 2 number (i.e. 16 -> 8).  it'll continue to round down
	 * and retry the allocation until the number of blocks to allocate
	 * is smaller than the number of blocks per page.
	 */
	nxlen = xlen;
	if ((rc = extBalloc(ip, hint ? hint : INOHINT(ip), &nxlen, &nxaddr))) {
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
		return (rc);
	}

	/* Allocate blocks to quota. */
	if (DQUOT_ALLOC_BLOCK(ip, nxlen)) {
		dbFree(ip, nxaddr, (s64) nxlen);
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
		return -EDQUOT;
	}

	/* determine the value of the extent flag */
	xflag = abnr ? XAD_NOTRECORDED : 0;

	/* if we can extend the hint extent to cover the current request,
	 * extend it.  otherwise, insert a new extent to
	 * cover the current request.
	 */
	if (xaddr && xaddr == nxaddr)
		rc = xtExtend(0, ip, xoff, (int) nxlen, 0);
	else
		rc = xtInsert(0, ip, xflag, xoff, (int) nxlen, &nxaddr, 0);

	/* if the extend or insert failed,
	 * free the newly allocated blocks and return the error.
	 */
	if (rc) {
		dbFree(ip, nxaddr, nxlen);
		DQUOT_FREE_BLOCK(ip, nxlen);
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
		return (rc);
	}

	/* set the results of the extent allocation */
	XADaddress(xp, nxaddr);
	XADlength(xp, nxlen);
	XADoffset(xp, xoff);
	xp->flag = xflag;

	mark_inode_dirty(ip);

	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	/*
	 * COMMIT_SyncList flags an anonymous tlock on page that is on
	 * sync list.
	 * We need to commit the inode to get the page written disk.
	 */
	if (test_and_clear_cflag(COMMIT_Synclist,ip))
		jfs_commit_inode(ip, 0);

	return (0);
}


#ifdef _NOTYET
/*
 * NAME:        extRealloc()
 *
 * FUNCTION:    extend the allocation of a file extent containing a
 *		partial back last page.
 *
 * PARAMETERS:
 *	ip	- the inode of the file.
 *	cp	- cbuf for the partial backed last page.
 *	xlen	- request size of the resulting extent.
 *	xp	- pointer to an xad. on successful exit, the xad
 *		  describes the newly allocated extent.
 *	abnr	- bool indicating whether the newly allocated extent
 *		  should be marked as allocated but not recorded.
 *
 * RETURN VALUES:
 *      0       - success
 *      -EIO	- i/o error.
 *      -ENOSPC	- insufficient disk resources.
 */
int extRealloc(struct inode *ip, s64 nxlen, xad_t * xp, bool abnr)
{
	struct super_block *sb = ip->i_sb;
	s64 xaddr, xlen, nxaddr, delta, xoff;
	s64 ntail, nextend, ninsert;
	int rc, nbperpage = JFS_SBI(sb)->nbperpage;
	int xflag;

	/* This blocks if we are low on resources */
	txBeginAnon(ip->i_sb);

	mutex_lock(&JFS_IP(ip)->commit_mutex);
	/* validate extent length */
	if (nxlen > MAXXLEN)
		nxlen = MAXXLEN;

	/* get the extend (partial) page's disk block address and
	 * number of blocks.
	 */
	xaddr = addressXAD(xp);
	xlen = lengthXAD(xp);
	xoff = offsetXAD(xp);

	/* if the extend page is abnr and if the request is for
	 * the extent to be allocated and recorded,
	 * make the page allocated and recorded.
	 */
	if ((xp->flag & XAD_NOTRECORDED) && !abnr) {
		xp->flag = 0;
		if ((rc = xtUpdate(0, ip, xp)))
			goto exit;
	}

	/* try to allocated the request number of blocks for the
	 * extent.  dbRealloc() first tries to satisfy the request
	 * by extending the allocation in place. otherwise, it will
	 * try to allocate a new set of blocks large enough for the
	 * request.  in satisfying a request, dbReAlloc() may allocate
	 * less than what was request but will always allocate enough
	 * space as to satisfy the extend page.
	 */
	if ((rc = extBrealloc(ip, xaddr, xlen, &nxlen, &nxaddr)))
		goto exit;

	/* Allocat blocks to quota. */
	if (DQUOT_ALLOC_BLOCK(ip, nxlen)) {
		dbFree(ip, nxaddr, (s64) nxlen);
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
		return -EDQUOT;
	}

	delta = nxlen - xlen;

	/* check if the extend page is not abnr but the request is abnr
	 * and the allocated disk space is for more than one page.  if this
	 * is the case, there is a miss match of abnr between the extend page
	 * and the one or more pages following the extend page.  as a result,
	 * two extents will have to be manipulated. the first will be that
	 * of the extent of the extend page and will be manipulated thru
	 * an xtExtend() or an xtTailgate(), depending upon whether the
	 * disk allocation occurred as an inplace extension.  the second
	 * extent will be manipulated (created) through an xtInsert() and
	 * will be for the pages following the extend page.
	 */
	if (abnr && (!(xp->flag & XAD_NOTRECORDED)) && (nxlen > nbperpage)) {
		ntail = nbperpage;
		nextend = ntail - xlen;
		ninsert = nxlen - nbperpage;

		xflag = XAD_NOTRECORDED;
	} else {
		ntail = nxlen;
		nextend = delta;
		ninsert = 0;

		xflag = xp->flag;
	}

	/* if we were able to extend the disk allocation in place,
	 * extend the extent.  otherwise, move the extent to a
	 * new disk location.
	 */
	if (xaddr == nxaddr) {
		/* extend the extent */
		if ((rc = xtExtend(0, ip, xoff + xlen, (int) nextend, 0))) {
			dbFree(ip, xaddr + xlen, delta);
			DQUOT_FREE_BLOCK(ip, nxlen);
			goto exit;
		}
	} else {
		/*
		 * move the extent to a new location:
		 *
		 * xtTailgate() accounts for relocated tail extent;
		 */
		if ((rc = xtTailgate(0, ip, xoff, (int) ntail, nxaddr, 0))) {
			dbFree(ip, nxaddr, nxlen);
			DQUOT_FREE_BLOCK(ip, nxlen);
			goto exit;
		}
	}


	/* check if we need to also insert a new extent */
	if (ninsert) {
		/* perform the insert.  if it fails, free the blocks
		 * to be inserted and make it appear that we only did
		 * the xtExtend() or xtTailgate() above.
		 */
		xaddr = nxaddr + ntail;
		if (xtInsert (0, ip, xflag, xoff + ntail, (int) ninsert,
			      &xaddr, 0)) {
			dbFree(ip, xaddr, (s64) ninsert);
			delta = nextend;
			nxlen = ntail;
			xflag = 0;
		}
	}

	/* set the return results */
	XADaddress(xp, nxaddr);
	XADlength(xp, nxlen);
	XADoffset(xp, xoff);
	xp->flag = xflag;

	mark_inode_dirty(ip);
exit:
	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	return (rc);
}
#endif			/* _NOTYET */


/*
 * NAME:        extHint()
 *
 * FUNCTION:    produce an extent allocation hint for a file offset.
 *
 * PARAMETERS:
 *	ip	- the inode of the file.
 *	offset  - file offset for which the hint is needed.
 *	xp	- pointer to the xad that is to be filled in with
 *		  the hint.
 *
 * RETURN VALUES:
 *      0       - success
 *      -EIO	- i/o error.
 */
int extHint(struct inode *ip, s64 offset, xad_t * xp)
{
	struct super_block *sb = ip->i_sb;
	struct xadlist xadl;
	struct lxdlist lxdl;
	lxd_t lxd;
	s64 prev;
	int rc, nbperpage = JFS_SBI(sb)->nbperpage;

	/* init the hint as "no hint provided" */
	XADaddress(xp, 0);

	/* determine the starting extent offset of the page previous
	 * to the page containing the offset.
	 */
	prev = ((offset & ~POFFSET) >> JFS_SBI(sb)->l2bsize) - nbperpage;

	/* if the offsets in the first page of the file,
	 * no hint provided.
	 */
	if (prev < 0)
		return (0);

	/* prepare to lookup the previous page's extent info */
	lxdl.maxnlxd = 1;
	lxdl.nlxd = 1;
	lxdl.lxd = &lxd;
	LXDoffset(&lxd, prev)
	    LXDlength(&lxd, nbperpage);

	xadl.maxnxad = 1;
	xadl.nxad = 0;
	xadl.xad = xp;

	/* perform the lookup */
	if ((rc = xtLookupList(ip, &lxdl, &xadl, 0)))
		return (rc);

	/* check if not extent exists for the previous page.
	 * this is possible for sparse files.
	 */
	if (xadl.nxad == 0) {
//              assert(ISSPARSE(ip));
		return (0);
	}

	/* only preserve the abnr flag within the xad flags
	 * of the returned hint.
	 */
	xp->flag &= XAD_NOTRECORDED;

        if(xadl.nxad != 1 || lengthXAD(xp) != nbperpage) {
		jfs_error(ip->i_sb, "extHint: corrupt xtree");
		return -EIO;
        }

	return (0);
}


/*
 * NAME:        extRecord()
 *
 * FUNCTION:    change a page with a file from not recorded to recorded.
 *
 * PARAMETERS:
 *	ip	- inode of the file.
 *	cp	- cbuf of the file page.
 *
 * RETURN VALUES:
 *      0       - success
 *      -EIO	- i/o error.
 *      -ENOSPC	- insufficient disk resources.
 */
int extRecord(struct inode *ip, xad_t * xp)
{
	int rc;

	txBeginAnon(ip->i_sb);

	mutex_lock(&JFS_IP(ip)->commit_mutex);

	/* update the extent */
	rc = xtUpdate(0, ip, xp);

	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	return rc;
}


#ifdef _NOTYET
/*
 * NAME:        extFill()
 *
 * FUNCTION:    allocate disk space for a file page that represents
 *		a file hole.
 *
 * PARAMETERS:
 *	ip	- the inode of the file.
 *	cp	- cbuf of the file page represent the hole.
 *
 * RETURN VALUES:
 *      0       - success
 *      -EIO	- i/o error.
 *      -ENOSPC	- insufficient disk resources.
 */
int extFill(struct inode *ip, xad_t * xp)
{
	int rc, nbperpage = JFS_SBI(ip->i_sb)->nbperpage;
	s64 blkno = offsetXAD(xp) >> ip->i_blkbits;

//      assert(ISSPARSE(ip));

	/* initialize the extent allocation hint */
	XADaddress(xp, 0);

	/* allocate an extent to fill the hole */
	if ((rc = extAlloc(ip, nbperpage, blkno, xp, false)))
		return (rc);

	assert(lengthPXD(xp) == nbperpage);

	return (0);
}
#endif			/* _NOTYET */


/*
 * NAME:	extBalloc()
 *
 * FUNCTION:    allocate disk blocks to form an extent.
 *
 *		initially, we will try to allocate disk blocks for the
 *		requested size (nblocks).  if this fails (nblocks
 *		contiguous free blocks not avaliable), we'll try to allocate
 *		a smaller number of blocks (producing a smaller extent), with
 *		this smaller number of blocks consisting of the requested
 *		number of blocks rounded down to the next smaller power of 2
 *		number (i.e. 16 -> 8).  we'll continue to round down and
 *		retry the allocation until the number of blocks to allocate
 *		is smaller than the number of blocks per page.
 *
 * PARAMETERS:
 *	ip	 - the inode of the file.
 *	hint	 - disk block number to be used as an allocation hint.
 *	*nblocks - pointer to an s64 value.  on entry, this value specifies
 *		   the desired number of block to be allocated. on successful
 *		   exit, this value is set to the number of blocks actually
 *		   allocated.
 *	blkno	 - pointer to a block address that is filled in on successful
 *		   return with the starting block number of the newly
 *		   allocated block range.
 *
 * RETURN VALUES:
 *      0       - success
 *      -EIO	- i/o error.
 *      -ENOSPC	- insufficient disk resources.
 */
static int
extBalloc(struct inode *ip, s64 hint, s64 * nblocks, s64 * blkno)
{
	struct jfs_inode_info *ji = JFS_IP(ip);
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	s64 nb, nblks, daddr, max;
	int rc, nbperpage = sbi->nbperpage;
	struct bmap *bmp = sbi->bmap;
	int ag;

	/* get the number of blocks to initially attempt to allocate.
	 * we'll first try the number of blocks requested unless this
	 * number is greater than the maximum number of contiguous free
	 * blocks in the map. in that case, we'll start off with the
	 * maximum free.
	 */
	max = (s64) 1 << bmp->db_maxfreebud;
	if (*nblocks >= max && *nblocks > nbperpage)
		nb = nblks = (max > nbperpage) ? max : nbperpage;
	else
		nb = nblks = *nblocks;

	/* try to allocate blocks */
	while ((rc = dbAlloc(ip, hint, nb, &daddr)) != 0) {
		/* if something other than an out of space error,
		 * stop and return this error.
		 */
		if (rc != -ENOSPC)
			return (rc);

		/* decrease the allocation request size */
		nb = min(nblks, extRoundDown(nb));

		/* give up if we cannot cover a page */
		if (nb < nbperpage)
			return (rc);
	}

	*nblocks = nb;
	*blkno = daddr;

	if (S_ISREG(ip->i_mode) && (ji->fileset == FILESYSTEM_I)) {
		ag = BLKTOAG(daddr, sbi);
		spin_lock_irq(&ji->ag_lock);
		if (ji->active_ag == -1) {
			atomic_inc(&bmp->db_active[ag]);
			ji->active_ag = ag;
		} else if (ji->active_ag != ag) {
			atomic_dec(&bmp->db_active[ji->active_ag]);
			atomic_inc(&bmp->db_active[ag]);
			ji->active_ag = ag;
		}
		spin_unlock_irq(&ji->ag_lock);
	}

	return (0);
}


#ifdef _NOTYET
/*
 * NAME:	extBrealloc()
 *
 * FUNCTION:    attempt to extend an extent's allocation.
 *
 *		Initially, we will try to extend the extent's allocation
 *		in place.  If this fails, we'll try to move the extent
 *		to a new set of blocks.  If moving the extent, we initially
 *		will try to allocate disk blocks for the requested size
 *		(newnblks).  if this fails (new contiguous free blocks not
 *		avaliable), we'll try to allocate a smaller number of
 *		blocks (producing a smaller extent), with this smaller
 *		number of blocks consisting of the requested number of
 *		blocks rounded down to the next smaller power of 2
 *		number (i.e. 16 -> 8).  We'll continue to round down and
 *		retry the allocation until the number of blocks to allocate
 *		is smaller than the number of blocks per page.
 *
 * PARAMETERS:
 *	ip	 - the inode of the file.
 *	blkno    - starting block number of the extents current allocation.
 *	nblks    - number of blocks within the extents current allocation.
 *	newnblks - pointer to a s64 value.  on entry, this value is the
 *		   the new desired extent size (number of blocks).  on
 *		   successful exit, this value is set to the extent's actual
 *		   new size (new number of blocks).
 *	newblkno - the starting block number of the extents new allocation.
 *
 * RETURN VALUES:
 *      0       - success
 *      -EIO	- i/o error.
 *      -ENOSPC	- insufficient disk resources.
 */
static int
extBrealloc(struct inode *ip,
	    s64 blkno, s64 nblks, s64 * newnblks, s64 * newblkno)
{
	int rc;

	/* try to extend in place */
	if ((rc = dbExtend(ip, blkno, nblks, *newnblks - nblks)) == 0) {
		*newblkno = blkno;
		return (0);
	} else {
		if (rc != -ENOSPC)
			return (rc);
	}

	/* in place extension not possible.
	 * try to move the extent to a new set of blocks.
	 */
	return (extBalloc(ip, blkno, newnblks, newblkno));
}
#endif			/* _NOTYET */


/*
 * NAME:        extRoundDown()
 *
 * FUNCTION:    round down a specified number of blocks to the next
 *		smallest power of 2 number.
 *
 * PARAMETERS:
 *	nb	- the inode of the file.
 *
 * RETURN VALUES:
 *      next smallest power of 2 number.
 */
static s64 extRoundDown(s64 nb)
{
	int i;
	u64 m, k;

	for (i = 0, m = (u64) 1 << 63; i < 64; i++, m >>= 1) {
		if (m & nb)
			break;
	}

	i = 63 - i;
	k = (u64) 1 << i;
	k = ((k - 1) & nb) ? k : k >> 1;

	return (k);
}
