// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
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
static s64 extRoundDown(s64 nb);

#define DPD(a)		(printk("(a): %d\n",(a)))
#define DPC(a)		(printk("(a): %c\n",(a)))
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

#define DPD1(a)		(printk("(a): %d  ",(a)))
#define DPX(a)		(printk("(a): %08x\n",(a)))
#define DPX1(a)		(printk("(a): %08x  ",(a)))
#define DPS(a)		(printk("%s\n",(a)))
#define DPE(a)		(printk("\nENTERING: %s\n",(a)))
#define DPE1(a)		(printk("\nENTERING: %s",(a)))
#define DPS1(a)		(printk("  %s  ",(a)))


/*
 * NAME:	extAlloc()
 *
 * FUNCTION:	allocate an extent for a specified page range within a
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
 *	0	- success
 *	-EIO	- i/o error.
 *	-ENOSPC	- insufficient disk resources.
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
	 * if this fails (xlen contiguous free blocks not available), it'll
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
	rc = dquot_alloc_block(ip, nxlen);
	if (rc) {
		dbFree(ip, nxaddr, (s64) nxlen);
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
		return rc;
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
		dquot_free_block(ip, nxlen);
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

/*
 * NAME:	extHint()
 *
 * FUNCTION:	produce an extent allocation hint for a file offset.
 *
 * PARAMETERS:
 *	ip	- the inode of the file.
 *	offset  - file offset for which the hint is needed.
 *	xp	- pointer to the xad that is to be filled in with
 *		  the hint.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 */
int extHint(struct inode *ip, s64 offset, xad_t * xp)
{
	struct super_block *sb = ip->i_sb;
	int nbperpage = JFS_SBI(sb)->nbperpage;
	s64 prev;
	int rc = 0;
	s64 xaddr;
	int xlen;
	int xflag;

	/* init the hint as "no hint provided" */
	XADaddress(xp, 0);

	/* determine the starting extent offset of the page previous
	 * to the page containing the offset.
	 */
	prev = ((offset & ~POFFSET) >> JFS_SBI(sb)->l2bsize) - nbperpage;

	/* if the offset is in the first page of the file, no hint provided.
	 */
	if (prev < 0)
		goto out;

	rc = xtLookup(ip, prev, nbperpage, &xflag, &xaddr, &xlen, 0);

	if ((rc == 0) && xlen) {
		if (xlen != nbperpage) {
			jfs_error(ip->i_sb, "corrupt xtree\n");
			rc = -EIO;
		}
		XADaddress(xp, xaddr);
		XADlength(xp, xlen);
		XADoffset(xp, prev);
		/*
		 * only preserve the abnr flag within the xad flags
		 * of the returned hint.
		 */
		xp->flag  = xflag & XAD_NOTRECORDED;
	} else
		rc = 0;

out:
	return (rc);
}


/*
 * NAME:	extRecord()
 *
 * FUNCTION:	change a page with a file from not recorded to recorded.
 *
 * PARAMETERS:
 *	ip	- inode of the file.
 *	cp	- cbuf of the file page.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error.
 *	-ENOSPC	- insufficient disk resources.
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

/*
 * NAME:	extBalloc()
 *
 * FUNCTION:	allocate disk blocks to form an extent.
 *
 *		initially, we will try to allocate disk blocks for the
 *		requested size (nblocks).  if this fails (nblocks
 *		contiguous free blocks not available), we'll try to allocate
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
 *	0	- success
 *	-EIO	- i/o error.
 *	-ENOSPC	- insufficient disk resources.
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

	/* give up if no space left */
	if (bmp->db_maxfreebud == -1)
		return -ENOSPC;

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

/*
 * NAME:	extRoundDown()
 *
 * FUNCTION:	round down a specified number of blocks to the next
 *		smallest power of 2 number.
 *
 * PARAMETERS:
 *	nb	- the inode of the file.
 *
 * RETURN VALUES:
 *	next smallest power of 2 number.
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
