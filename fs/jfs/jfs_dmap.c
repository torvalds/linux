// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Tino Reichardt, 2012
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include "jfs_incore.h"
#include "jfs_superblock.h"
#include "jfs_dmap.h"
#include "jfs_imap.h"
#include "jfs_lock.h"
#include "jfs_metapage.h"
#include "jfs_debug.h"
#include "jfs_discard.h"

/*
 *	SERIALIZATION of the Block Allocation Map.
 *
 *	the working state of the block allocation map is accessed in
 *	two directions:
 *
 *	1) allocation and free requests that start at the dmap
 *	   level and move up through the dmap control pages (i.e.
 *	   the vast majority of requests).
 *
 *	2) allocation requests that start at dmap control page
 *	   level and work down towards the dmaps.
 *
 *	the serialization scheme used here is as follows.
 *
 *	requests which start at the bottom are serialized against each
 *	other through buffers and each requests holds onto its buffers
 *	as it works it way up from a single dmap to the required level
 *	of dmap control page.
 *	requests that start at the top are serialized against each other
 *	and request that start from the bottom by the multiple read/single
 *	write inode lock of the bmap inode. requests starting at the top
 *	take this lock in write mode while request starting at the bottom
 *	take the lock in read mode.  a single top-down request may proceed
 *	exclusively while multiple bottoms-up requests may proceed
 *	simultaneously (under the protection of busy buffers).
 *
 *	in addition to information found in dmaps and dmap control pages,
 *	the working state of the block allocation map also includes read/
 *	write information maintained in the bmap descriptor (i.e. total
 *	free block count, allocation group level free block counts).
 *	a single exclusive lock (BMAP_LOCK) is used to guard this information
 *	in the face of multiple-bottoms up requests.
 *	(lock ordering: IREAD_LOCK, BMAP_LOCK);
 *
 *	accesses to the persistent state of the block allocation map (limited
 *	to the persistent bitmaps in dmaps) is guarded by (busy) buffers.
 */

#define BMAP_LOCK_INIT(bmp)	mutex_init(&bmp->db_bmaplock)
#define BMAP_LOCK(bmp)		mutex_lock(&bmp->db_bmaplock)
#define BMAP_UNLOCK(bmp)	mutex_unlock(&bmp->db_bmaplock)

/*
 * forward references
 */
static void dbAllocBits(struct bmap * bmp, struct dmap * dp, s64 blkno,
			int nblocks);
static void dbSplit(dmtree_t * tp, int leafno, int splitsz, int newval);
static int dbBackSplit(dmtree_t * tp, int leafno);
static int dbJoin(dmtree_t * tp, int leafno, int newval);
static void dbAdjTree(dmtree_t * tp, int leafno, int newval);
static int dbAdjCtl(struct bmap * bmp, s64 blkno, int newval, int alloc,
		    int level);
static int dbAllocAny(struct bmap * bmp, s64 nblocks, int l2nb, s64 * results);
static int dbAllocNext(struct bmap * bmp, struct dmap * dp, s64 blkno,
		       int nblocks);
static int dbAllocNear(struct bmap * bmp, struct dmap * dp, s64 blkno,
		       int nblocks,
		       int l2nb, s64 * results);
static int dbAllocDmap(struct bmap * bmp, struct dmap * dp, s64 blkno,
		       int nblocks);
static int dbAllocDmapLev(struct bmap * bmp, struct dmap * dp, int nblocks,
			  int l2nb,
			  s64 * results);
static int dbAllocAG(struct bmap * bmp, int agno, s64 nblocks, int l2nb,
		     s64 * results);
static int dbAllocCtl(struct bmap * bmp, s64 nblocks, int l2nb, s64 blkno,
		      s64 * results);
static int dbExtend(struct inode *ip, s64 blkno, s64 nblocks, s64 addnblocks);
static int dbFindBits(u32 word, int l2nb);
static int dbFindCtl(struct bmap * bmp, int l2nb, int level, s64 * blkno);
static int dbFindLeaf(dmtree_t * tp, int l2nb, int *leafidx);
static int dbFreeBits(struct bmap * bmp, struct dmap * dp, s64 blkno,
		      int nblocks);
static int dbFreeDmap(struct bmap * bmp, struct dmap * dp, s64 blkno,
		      int nblocks);
static int dbMaxBud(u8 * cp);
static int blkstol2(s64 nb);

static int cntlz(u32 value);
static int cnttz(u32 word);

static int dbAllocDmapBU(struct bmap * bmp, struct dmap * dp, s64 blkno,
			 int nblocks);
static int dbInitDmap(struct dmap * dp, s64 blkno, int nblocks);
static int dbInitDmapTree(struct dmap * dp);
static int dbInitTree(struct dmaptree * dtp);
static int dbInitDmapCtl(struct dmapctl * dcp, int level, int i);
static int dbGetL2AGSize(s64 nblocks);

/*
 *	buddy table
 *
 * table used for determining buddy sizes within characters of
 * dmap bitmap words.  the characters themselves serve as indexes
 * into the table, with the table elements yielding the maximum
 * binary buddy of free bits within the character.
 */
static const s8 budtab[256] = {
	3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	2, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, -1
};

/*
 * NAME:	dbMount()
 *
 * FUNCTION:	initializate the block allocation map.
 *
 *		memory is allocated for the in-core bmap descriptor and
 *		the in-core descriptor is initialized from disk.
 *
 * PARAMETERS:
 *	ipbmap	- pointer to in-core inode for the block map.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOMEM	- insufficient memory
 *	-EIO	- i/o error
 *	-EINVAL - wrong bmap data
 */
int dbMount(struct inode *ipbmap)
{
	struct bmap *bmp;
	struct dbmap_disk *dbmp_le;
	struct metapage *mp;
	int i, err;

	/*
	 * allocate/initialize the in-memory bmap descriptor
	 */
	/* allocate memory for the in-memory bmap descriptor */
	bmp = kmalloc(sizeof(struct bmap), GFP_KERNEL);
	if (bmp == NULL)
		return -ENOMEM;

	/* read the on-disk bmap descriptor. */
	mp = read_metapage(ipbmap,
			   BMAPBLKNO << JFS_SBI(ipbmap->i_sb)->l2nbperpage,
			   PSIZE, 0);
	if (mp == NULL) {
		err = -EIO;
		goto err_kfree_bmp;
	}

	/* copy the on-disk bmap descriptor to its in-memory version. */
	dbmp_le = (struct dbmap_disk *) mp->data;
	bmp->db_mapsize = le64_to_cpu(dbmp_le->dn_mapsize);
	bmp->db_nfree = le64_to_cpu(dbmp_le->dn_nfree);
	bmp->db_l2nbperpage = le32_to_cpu(dbmp_le->dn_l2nbperpage);
	bmp->db_numag = le32_to_cpu(dbmp_le->dn_numag);
	if (!bmp->db_numag) {
		err = -EINVAL;
		goto err_release_metapage;
	}

	bmp->db_maxlevel = le32_to_cpu(dbmp_le->dn_maxlevel);
	bmp->db_maxag = le32_to_cpu(dbmp_le->dn_maxag);
	bmp->db_agpref = le32_to_cpu(dbmp_le->dn_agpref);
	bmp->db_aglevel = le32_to_cpu(dbmp_le->dn_aglevel);
	bmp->db_agheight = le32_to_cpu(dbmp_le->dn_agheight);
	bmp->db_agwidth = le32_to_cpu(dbmp_le->dn_agwidth);
	bmp->db_agstart = le32_to_cpu(dbmp_le->dn_agstart);
	bmp->db_agl2size = le32_to_cpu(dbmp_le->dn_agl2size);
	if (bmp->db_agl2size > L2MAXL2SIZE - L2MAXAG) {
		err = -EINVAL;
		goto err_release_metapage;
	}

	if (((bmp->db_mapsize - 1) >> bmp->db_agl2size) > MAXAG) {
		err = -EINVAL;
		goto err_release_metapage;
	}

	for (i = 0; i < MAXAG; i++)
		bmp->db_agfree[i] = le64_to_cpu(dbmp_le->dn_agfree[i]);
	bmp->db_agsize = le64_to_cpu(dbmp_le->dn_agsize);
	bmp->db_maxfreebud = dbmp_le->dn_maxfreebud;

	/* release the buffer. */
	release_metapage(mp);

	/* bind the bmap inode and the bmap descriptor to each other. */
	bmp->db_ipbmap = ipbmap;
	JFS_SBI(ipbmap->i_sb)->bmap = bmp;

	memset(bmp->db_active, 0, sizeof(bmp->db_active));

	/*
	 * allocate/initialize the bmap lock
	 */
	BMAP_LOCK_INIT(bmp);

	return (0);

err_release_metapage:
	release_metapage(mp);
err_kfree_bmp:
	kfree(bmp);
	return err;
}


/*
 * NAME:	dbUnmount()
 *
 * FUNCTION:	terminate the block allocation map in preparation for
 *		file system unmount.
 *
 *		the in-core bmap descriptor is written to disk and
 *		the memory for this descriptor is freed.
 *
 * PARAMETERS:
 *	ipbmap	- pointer to in-core inode for the block map.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error
 */
int dbUnmount(struct inode *ipbmap, int mounterror)
{
	struct bmap *bmp = JFS_SBI(ipbmap->i_sb)->bmap;

	if (!(mounterror || isReadOnly(ipbmap)))
		dbSync(ipbmap);

	/*
	 * Invalidate the page cache buffers
	 */
	truncate_inode_pages(ipbmap->i_mapping, 0);

	/* free the memory for the in-memory bmap. */
	kfree(bmp);

	return (0);
}

/*
 *	dbSync()
 */
int dbSync(struct inode *ipbmap)
{
	struct dbmap_disk *dbmp_le;
	struct bmap *bmp = JFS_SBI(ipbmap->i_sb)->bmap;
	struct metapage *mp;
	int i;

	/*
	 * write bmap global control page
	 */
	/* get the buffer for the on-disk bmap descriptor. */
	mp = read_metapage(ipbmap,
			   BMAPBLKNO << JFS_SBI(ipbmap->i_sb)->l2nbperpage,
			   PSIZE, 0);
	if (mp == NULL) {
		jfs_err("dbSync: read_metapage failed!");
		return -EIO;
	}
	/* copy the in-memory version of the bmap to the on-disk version */
	dbmp_le = (struct dbmap_disk *) mp->data;
	dbmp_le->dn_mapsize = cpu_to_le64(bmp->db_mapsize);
	dbmp_le->dn_nfree = cpu_to_le64(bmp->db_nfree);
	dbmp_le->dn_l2nbperpage = cpu_to_le32(bmp->db_l2nbperpage);
	dbmp_le->dn_numag = cpu_to_le32(bmp->db_numag);
	dbmp_le->dn_maxlevel = cpu_to_le32(bmp->db_maxlevel);
	dbmp_le->dn_maxag = cpu_to_le32(bmp->db_maxag);
	dbmp_le->dn_agpref = cpu_to_le32(bmp->db_agpref);
	dbmp_le->dn_aglevel = cpu_to_le32(bmp->db_aglevel);
	dbmp_le->dn_agheight = cpu_to_le32(bmp->db_agheight);
	dbmp_le->dn_agwidth = cpu_to_le32(bmp->db_agwidth);
	dbmp_le->dn_agstart = cpu_to_le32(bmp->db_agstart);
	dbmp_le->dn_agl2size = cpu_to_le32(bmp->db_agl2size);
	for (i = 0; i < MAXAG; i++)
		dbmp_le->dn_agfree[i] = cpu_to_le64(bmp->db_agfree[i]);
	dbmp_le->dn_agsize = cpu_to_le64(bmp->db_agsize);
	dbmp_le->dn_maxfreebud = bmp->db_maxfreebud;

	/* write the buffer */
	write_metapage(mp);

	/*
	 * write out dirty pages of bmap
	 */
	filemap_write_and_wait(ipbmap->i_mapping);

	diWriteSpecial(ipbmap, 0);

	return (0);
}

/*
 * NAME:	dbFree()
 *
 * FUNCTION:	free the specified block range from the working block
 *		allocation map.
 *
 *		the blocks will be free from the working map one dmap
 *		at a time.
 *
 * PARAMETERS:
 *	ip	- pointer to in-core inode;
 *	blkno	- starting block number to be freed.
 *	nblocks	- number of blocks to be freed.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error
 */
int dbFree(struct inode *ip, s64 blkno, s64 nblocks)
{
	struct metapage *mp;
	struct dmap *dp;
	int nb, rc;
	s64 lblkno, rem;
	struct inode *ipbmap = JFS_SBI(ip->i_sb)->ipbmap;
	struct bmap *bmp = JFS_SBI(ip->i_sb)->bmap;
	struct super_block *sb = ipbmap->i_sb;

	IREAD_LOCK(ipbmap, RDWRLOCK_DMAP);

	/* block to be freed better be within the mapsize. */
	if (unlikely((blkno == 0) || (blkno + nblocks > bmp->db_mapsize))) {
		IREAD_UNLOCK(ipbmap);
		printk(KERN_ERR "blkno = %Lx, nblocks = %Lx\n",
		       (unsigned long long) blkno,
		       (unsigned long long) nblocks);
		jfs_error(ip->i_sb, "block to be freed is outside the map\n");
		return -EIO;
	}

	/**
	 * TRIM the blocks, when mounted with discard option
	 */
	if (JFS_SBI(sb)->flag & JFS_DISCARD)
		if (JFS_SBI(sb)->minblks_trim <= nblocks)
			jfs_issue_discard(ipbmap, blkno, nblocks);

	/*
	 * free the blocks a dmap at a time.
	 */
	mp = NULL;
	for (rem = nblocks; rem > 0; rem -= nb, blkno += nb) {
		/* release previous dmap if any */
		if (mp) {
			write_metapage(mp);
		}

		/* get the buffer for the current dmap. */
		lblkno = BLKTODMAP(blkno, bmp->db_l2nbperpage);
		mp = read_metapage(ipbmap, lblkno, PSIZE, 0);
		if (mp == NULL) {
			IREAD_UNLOCK(ipbmap);
			return -EIO;
		}
		dp = (struct dmap *) mp->data;

		/* determine the number of blocks to be freed from
		 * this dmap.
		 */
		nb = min(rem, BPERDMAP - (blkno & (BPERDMAP - 1)));

		/* free the blocks. */
		if ((rc = dbFreeDmap(bmp, dp, blkno, nb))) {
			jfs_error(ip->i_sb, "error in block map\n");
			release_metapage(mp);
			IREAD_UNLOCK(ipbmap);
			return (rc);
		}
	}

	/* write the last buffer. */
	if (mp)
		write_metapage(mp);

	IREAD_UNLOCK(ipbmap);

	return (0);
}


/*
 * NAME:	dbUpdatePMap()
 *
 * FUNCTION:	update the allocation state (free or allocate) of the
 *		specified block range in the persistent block allocation map.
 *
 *		the blocks will be updated in the persistent map one
 *		dmap at a time.
 *
 * PARAMETERS:
 *	ipbmap	- pointer to in-core inode for the block map.
 *	free	- 'true' if block range is to be freed from the persistent
 *		  map; 'false' if it is to be allocated.
 *	blkno	- starting block number of the range.
 *	nblocks	- number of contiguous blocks in the range.
 *	tblk	- transaction block;
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error
 */
int
dbUpdatePMap(struct inode *ipbmap,
	     int free, s64 blkno, s64 nblocks, struct tblock * tblk)
{
	int nblks, dbitno, wbitno, rbits;
	int word, nbits, nwords;
	struct bmap *bmp = JFS_SBI(ipbmap->i_sb)->bmap;
	s64 lblkno, rem, lastlblkno;
	u32 mask;
	struct dmap *dp;
	struct metapage *mp;
	struct jfs_log *log;
	int lsn, difft, diffp;
	unsigned long flags;

	/* the blocks better be within the mapsize. */
	if (blkno + nblocks > bmp->db_mapsize) {
		printk(KERN_ERR "blkno = %Lx, nblocks = %Lx\n",
		       (unsigned long long) blkno,
		       (unsigned long long) nblocks);
		jfs_error(ipbmap->i_sb, "blocks are outside the map\n");
		return -EIO;
	}

	/* compute delta of transaction lsn from log syncpt */
	lsn = tblk->lsn;
	log = (struct jfs_log *) JFS_SBI(tblk->sb)->log;
	logdiff(difft, lsn, log);

	/*
	 * update the block state a dmap at a time.
	 */
	mp = NULL;
	lastlblkno = 0;
	for (rem = nblocks; rem > 0; rem -= nblks, blkno += nblks) {
		/* get the buffer for the current dmap. */
		lblkno = BLKTODMAP(blkno, bmp->db_l2nbperpage);
		if (lblkno != lastlblkno) {
			if (mp) {
				write_metapage(mp);
			}

			mp = read_metapage(bmp->db_ipbmap, lblkno, PSIZE,
					   0);
			if (mp == NULL)
				return -EIO;
			metapage_wait_for_io(mp);
		}
		dp = (struct dmap *) mp->data;

		/* determine the bit number and word within the dmap of
		 * the starting block.  also determine how many blocks
		 * are to be updated within this dmap.
		 */
		dbitno = blkno & (BPERDMAP - 1);
		word = dbitno >> L2DBWORD;
		nblks = min(rem, (s64)BPERDMAP - dbitno);

		/* update the bits of the dmap words. the first and last
		 * words may only have a subset of their bits updated. if
		 * this is the case, we'll work against that word (i.e.
		 * partial first and/or last) only in a single pass.  a
		 * single pass will also be used to update all words that
		 * are to have all their bits updated.
		 */
		for (rbits = nblks; rbits > 0;
		     rbits -= nbits, dbitno += nbits) {
			/* determine the bit number within the word and
			 * the number of bits within the word.
			 */
			wbitno = dbitno & (DBWORD - 1);
			nbits = min(rbits, DBWORD - wbitno);

			/* check if only part of the word is to be updated. */
			if (nbits < DBWORD) {
				/* update (free or allocate) the bits
				 * in this word.
				 */
				mask =
				    (ONES << (DBWORD - nbits) >> wbitno);
				if (free)
					dp->pmap[word] &=
					    cpu_to_le32(~mask);
				else
					dp->pmap[word] |=
					    cpu_to_le32(mask);

				word += 1;
			} else {
				/* one or more words are to have all
				 * their bits updated.  determine how
				 * many words and how many bits.
				 */
				nwords = rbits >> L2DBWORD;
				nbits = nwords << L2DBWORD;

				/* update (free or allocate) the bits
				 * in these words.
				 */
				if (free)
					memset(&dp->pmap[word], 0,
					       nwords * 4);
				else
					memset(&dp->pmap[word], (int) ONES,
					       nwords * 4);

				word += nwords;
			}
		}

		/*
		 * update dmap lsn
		 */
		if (lblkno == lastlblkno)
			continue;

		lastlblkno = lblkno;

		LOGSYNC_LOCK(log, flags);
		if (mp->lsn != 0) {
			/* inherit older/smaller lsn */
			logdiff(diffp, mp->lsn, log);
			if (difft < diffp) {
				mp->lsn = lsn;

				/* move bp after tblock in logsync list */
				list_move(&mp->synclist, &tblk->synclist);
			}

			/* inherit younger/larger clsn */
			logdiff(difft, tblk->clsn, log);
			logdiff(diffp, mp->clsn, log);
			if (difft > diffp)
				mp->clsn = tblk->clsn;
		} else {
			mp->log = log;
			mp->lsn = lsn;

			/* insert bp after tblock in logsync list */
			log->count++;
			list_add(&mp->synclist, &tblk->synclist);

			mp->clsn = tblk->clsn;
		}
		LOGSYNC_UNLOCK(log, flags);
	}

	/* write the last buffer. */
	if (mp) {
		write_metapage(mp);
	}

	return (0);
}


/*
 * NAME:	dbNextAG()
 *
 * FUNCTION:	find the preferred allocation group for new allocations.
 *
 *		Within the allocation groups, we maintain a preferred
 *		allocation group which consists of a group with at least
 *		average free space.  It is the preferred group that we target
 *		new inode allocation towards.  The tie-in between inode
 *		allocation and block allocation occurs as we allocate the
 *		first (data) block of an inode and specify the inode (block)
 *		as the allocation hint for this block.
 *
 *		We try to avoid having more than one open file growing in
 *		an allocation group, as this will lead to fragmentation.
 *		This differs from the old OS/2 method of trying to keep
 *		empty ags around for large allocations.
 *
 * PARAMETERS:
 *	ipbmap	- pointer to in-core inode for the block map.
 *
 * RETURN VALUES:
 *	the preferred allocation group number.
 */
int dbNextAG(struct inode *ipbmap)
{
	s64 avgfree;
	int agpref;
	s64 hwm = 0;
	int i;
	int next_best = -1;
	struct bmap *bmp = JFS_SBI(ipbmap->i_sb)->bmap;

	BMAP_LOCK(bmp);

	/* determine the average number of free blocks within the ags. */
	avgfree = (u32)bmp->db_nfree / bmp->db_numag;

	/*
	 * if the current preferred ag does not have an active allocator
	 * and has at least average freespace, return it
	 */
	agpref = bmp->db_agpref;
	if ((atomic_read(&bmp->db_active[agpref]) == 0) &&
	    (bmp->db_agfree[agpref] >= avgfree))
		goto unlock;

	/* From the last preferred ag, find the next one with at least
	 * average free space.
	 */
	for (i = 0 ; i < bmp->db_numag; i++, agpref++) {
		if (agpref == bmp->db_numag)
			agpref = 0;

		if (atomic_read(&bmp->db_active[agpref]))
			/* open file is currently growing in this ag */
			continue;
		if (bmp->db_agfree[agpref] >= avgfree) {
			/* Return this one */
			bmp->db_agpref = agpref;
			goto unlock;
		} else if (bmp->db_agfree[agpref] > hwm) {
			/* Less than avg. freespace, but best so far */
			hwm = bmp->db_agfree[agpref];
			next_best = agpref;
		}
	}

	/*
	 * If no inactive ag was found with average freespace, use the
	 * next best
	 */
	if (next_best != -1)
		bmp->db_agpref = next_best;
	/* else leave db_agpref unchanged */
unlock:
	BMAP_UNLOCK(bmp);

	/* return the preferred group.
	 */
	return (bmp->db_agpref);
}

/*
 * NAME:	dbAlloc()
 *
 * FUNCTION:	attempt to allocate a specified number of contiguous free
 *		blocks from the working allocation block map.
 *
 *		the block allocation policy uses hints and a multi-step
 *		approach.
 *
 *		for allocation requests smaller than the number of blocks
 *		per dmap, we first try to allocate the new blocks
 *		immediately following the hint.  if these blocks are not
 *		available, we try to allocate blocks near the hint.  if
 *		no blocks near the hint are available, we next try to
 *		allocate within the same dmap as contains the hint.
 *
 *		if no blocks are available in the dmap or the allocation
 *		request is larger than the dmap size, we try to allocate
 *		within the same allocation group as contains the hint. if
 *		this does not succeed, we finally try to allocate anywhere
 *		within the aggregate.
 *
 *		we also try to allocate anywhere within the aggregate
 *		for allocation requests larger than the allocation group
 *		size or requests that specify no hint value.
 *
 * PARAMETERS:
 *	ip	- pointer to in-core inode;
 *	hint	- allocation hint.
 *	nblocks	- number of contiguous blocks in the range.
 *	results	- on successful return, set to the starting block number
 *		  of the newly allocated contiguous range.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 */
int dbAlloc(struct inode *ip, s64 hint, s64 nblocks, s64 * results)
{
	int rc, agno;
	struct inode *ipbmap = JFS_SBI(ip->i_sb)->ipbmap;
	struct bmap *bmp;
	struct metapage *mp;
	s64 lblkno, blkno;
	struct dmap *dp;
	int l2nb;
	s64 mapSize;
	int writers;

	/* assert that nblocks is valid */
	assert(nblocks > 0);

	/* get the log2 number of blocks to be allocated.
	 * if the number of blocks is not a log2 multiple,
	 * it will be rounded up to the next log2 multiple.
	 */
	l2nb = BLKSTOL2(nblocks);

	bmp = JFS_SBI(ip->i_sb)->bmap;

	mapSize = bmp->db_mapsize;

	/* the hint should be within the map */
	if (hint >= mapSize) {
		jfs_error(ip->i_sb, "the hint is outside the map\n");
		return -EIO;
	}

	/* if the number of blocks to be allocated is greater than the
	 * allocation group size, try to allocate anywhere.
	 */
	if (l2nb > bmp->db_agl2size) {
		IWRITE_LOCK(ipbmap, RDWRLOCK_DMAP);

		rc = dbAllocAny(bmp, nblocks, l2nb, results);

		goto write_unlock;
	}

	/*
	 * If no hint, let dbNextAG recommend an allocation group
	 */
	if (hint == 0)
		goto pref_ag;

	/* we would like to allocate close to the hint.  adjust the
	 * hint to the block following the hint since the allocators
	 * will start looking for free space starting at this point.
	 */
	blkno = hint + 1;

	if (blkno >= bmp->db_mapsize)
		goto pref_ag;

	agno = blkno >> bmp->db_agl2size;

	/* check if blkno crosses over into a new allocation group.
	 * if so, check if we should allow allocations within this
	 * allocation group.
	 */
	if ((blkno & (bmp->db_agsize - 1)) == 0)
		/* check if the AG is currently being written to.
		 * if so, call dbNextAG() to find a non-busy
		 * AG with sufficient free space.
		 */
		if (atomic_read(&bmp->db_active[agno]))
			goto pref_ag;

	/* check if the allocation request size can be satisfied from a
	 * single dmap.  if so, try to allocate from the dmap containing
	 * the hint using a tiered strategy.
	 */
	if (nblocks <= BPERDMAP) {
		IREAD_LOCK(ipbmap, RDWRLOCK_DMAP);

		/* get the buffer for the dmap containing the hint.
		 */
		rc = -EIO;
		lblkno = BLKTODMAP(blkno, bmp->db_l2nbperpage);
		mp = read_metapage(ipbmap, lblkno, PSIZE, 0);
		if (mp == NULL)
			goto read_unlock;

		dp = (struct dmap *) mp->data;

		/* first, try to satisfy the allocation request with the
		 * blocks beginning at the hint.
		 */
		if ((rc = dbAllocNext(bmp, dp, blkno, (int) nblocks))
		    != -ENOSPC) {
			if (rc == 0) {
				*results = blkno;
				mark_metapage_dirty(mp);
			}

			release_metapage(mp);
			goto read_unlock;
		}

		writers = atomic_read(&bmp->db_active[agno]);
		if ((writers > 1) ||
		    ((writers == 1) && (JFS_IP(ip)->active_ag != agno))) {
			/*
			 * Someone else is writing in this allocation
			 * group.  To avoid fragmenting, try another ag
			 */
			release_metapage(mp);
			IREAD_UNLOCK(ipbmap);
			goto pref_ag;
		}

		/* next, try to satisfy the allocation request with blocks
		 * near the hint.
		 */
		if ((rc =
		     dbAllocNear(bmp, dp, blkno, (int) nblocks, l2nb, results))
		    != -ENOSPC) {
			if (rc == 0)
				mark_metapage_dirty(mp);

			release_metapage(mp);
			goto read_unlock;
		}

		/* try to satisfy the allocation request with blocks within
		 * the same dmap as the hint.
		 */
		if ((rc = dbAllocDmapLev(bmp, dp, (int) nblocks, l2nb, results))
		    != -ENOSPC) {
			if (rc == 0)
				mark_metapage_dirty(mp);

			release_metapage(mp);
			goto read_unlock;
		}

		release_metapage(mp);
		IREAD_UNLOCK(ipbmap);
	}

	/* try to satisfy the allocation request with blocks within
	 * the same allocation group as the hint.
	 */
	IWRITE_LOCK(ipbmap, RDWRLOCK_DMAP);
	if ((rc = dbAllocAG(bmp, agno, nblocks, l2nb, results)) != -ENOSPC)
		goto write_unlock;

	IWRITE_UNLOCK(ipbmap);


      pref_ag:
	/*
	 * Let dbNextAG recommend a preferred allocation group
	 */
	agno = dbNextAG(ipbmap);
	IWRITE_LOCK(ipbmap, RDWRLOCK_DMAP);

	/* Try to allocate within this allocation group.  if that fails, try to
	 * allocate anywhere in the map.
	 */
	if ((rc = dbAllocAG(bmp, agno, nblocks, l2nb, results)) == -ENOSPC)
		rc = dbAllocAny(bmp, nblocks, l2nb, results);

      write_unlock:
	IWRITE_UNLOCK(ipbmap);

	return (rc);

      read_unlock:
	IREAD_UNLOCK(ipbmap);

	return (rc);
}

/*
 * NAME:	dbReAlloc()
 *
 * FUNCTION:	attempt to extend a current allocation by a specified
 *		number of blocks.
 *
 *		this routine attempts to satisfy the allocation request
 *		by first trying to extend the existing allocation in
 *		place by allocating the additional blocks as the blocks
 *		immediately following the current allocation.  if these
 *		blocks are not available, this routine will attempt to
 *		allocate a new set of contiguous blocks large enough
 *		to cover the existing allocation plus the additional
 *		number of blocks required.
 *
 * PARAMETERS:
 *	ip	    -  pointer to in-core inode requiring allocation.
 *	blkno	    -  starting block of the current allocation.
 *	nblocks	    -  number of contiguous blocks within the current
 *		       allocation.
 *	addnblocks  -  number of blocks to add to the allocation.
 *	results	-      on successful return, set to the starting block number
 *		       of the existing allocation if the existing allocation
 *		       was extended in place or to a newly allocated contiguous
 *		       range if the existing allocation could not be extended
 *		       in place.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 */
int
dbReAlloc(struct inode *ip,
	  s64 blkno, s64 nblocks, s64 addnblocks, s64 * results)
{
	int rc;

	/* try to extend the allocation in place.
	 */
	if ((rc = dbExtend(ip, blkno, nblocks, addnblocks)) == 0) {
		*results = blkno;
		return (0);
	} else {
		if (rc != -ENOSPC)
			return (rc);
	}

	/* could not extend the allocation in place, so allocate a
	 * new set of blocks for the entire request (i.e. try to get
	 * a range of contiguous blocks large enough to cover the
	 * existing allocation plus the additional blocks.)
	 */
	return (dbAlloc
		(ip, blkno + nblocks - 1, addnblocks + nblocks, results));
}


/*
 * NAME:	dbExtend()
 *
 * FUNCTION:	attempt to extend a current allocation by a specified
 *		number of blocks.
 *
 *		this routine attempts to satisfy the allocation request
 *		by first trying to extend the existing allocation in
 *		place by allocating the additional blocks as the blocks
 *		immediately following the current allocation.
 *
 * PARAMETERS:
 *	ip	    -  pointer to in-core inode requiring allocation.
 *	blkno	    -  starting block of the current allocation.
 *	nblocks	    -  number of contiguous blocks within the current
 *		       allocation.
 *	addnblocks  -  number of blocks to add to the allocation.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 */
static int dbExtend(struct inode *ip, s64 blkno, s64 nblocks, s64 addnblocks)
{
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);
	s64 lblkno, lastblkno, extblkno;
	uint rel_block;
	struct metapage *mp;
	struct dmap *dp;
	int rc;
	struct inode *ipbmap = sbi->ipbmap;
	struct bmap *bmp;

	/*
	 * We don't want a non-aligned extent to cross a page boundary
	 */
	if (((rel_block = blkno & (sbi->nbperpage - 1))) &&
	    (rel_block + nblocks + addnblocks > sbi->nbperpage))
		return -ENOSPC;

	/* get the last block of the current allocation */
	lastblkno = blkno + nblocks - 1;

	/* determine the block number of the block following
	 * the existing allocation.
	 */
	extblkno = lastblkno + 1;

	IREAD_LOCK(ipbmap, RDWRLOCK_DMAP);

	/* better be within the file system */
	bmp = sbi->bmap;
	if (lastblkno < 0 || lastblkno >= bmp->db_mapsize) {
		IREAD_UNLOCK(ipbmap);
		jfs_error(ip->i_sb, "the block is outside the filesystem\n");
		return -EIO;
	}

	/* we'll attempt to extend the current allocation in place by
	 * allocating the additional blocks as the blocks immediately
	 * following the current allocation.  we only try to extend the
	 * current allocation in place if the number of additional blocks
	 * can fit into a dmap, the last block of the current allocation
	 * is not the last block of the file system, and the start of the
	 * inplace extension is not on an allocation group boundary.
	 */
	if (addnblocks > BPERDMAP || extblkno >= bmp->db_mapsize ||
	    (extblkno & (bmp->db_agsize - 1)) == 0) {
		IREAD_UNLOCK(ipbmap);
		return -ENOSPC;
	}

	/* get the buffer for the dmap containing the first block
	 * of the extension.
	 */
	lblkno = BLKTODMAP(extblkno, bmp->db_l2nbperpage);
	mp = read_metapage(ipbmap, lblkno, PSIZE, 0);
	if (mp == NULL) {
		IREAD_UNLOCK(ipbmap);
		return -EIO;
	}

	dp = (struct dmap *) mp->data;

	/* try to allocate the blocks immediately following the
	 * current allocation.
	 */
	rc = dbAllocNext(bmp, dp, extblkno, (int) addnblocks);

	IREAD_UNLOCK(ipbmap);

	/* were we successful ? */
	if (rc == 0)
		write_metapage(mp);
	else
		/* we were not successful */
		release_metapage(mp);

	return (rc);
}


/*
 * NAME:	dbAllocNext()
 *
 * FUNCTION:	attempt to allocate the blocks of the specified block
 *		range within a dmap.
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	dp	-  pointer to dmap.
 *	blkno	-  starting block number of the range.
 *	nblocks	-  number of contiguous free blocks of the range.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 *
 * serialization: IREAD_LOCK(ipbmap) held on entry/exit;
 */
static int dbAllocNext(struct bmap * bmp, struct dmap * dp, s64 blkno,
		       int nblocks)
{
	int dbitno, word, rembits, nb, nwords, wbitno, nw;
	int l2size;
	s8 *leaf;
	u32 mask;

	if (dp->tree.leafidx != cpu_to_le32(LEAFIND)) {
		jfs_error(bmp->db_ipbmap->i_sb, "Corrupt dmap page\n");
		return -EIO;
	}

	/* pick up a pointer to the leaves of the dmap tree.
	 */
	leaf = dp->tree.stree + le32_to_cpu(dp->tree.leafidx);

	/* determine the bit number and word within the dmap of the
	 * starting block.
	 */
	dbitno = blkno & (BPERDMAP - 1);
	word = dbitno >> L2DBWORD;

	/* check if the specified block range is contained within
	 * this dmap.
	 */
	if (dbitno + nblocks > BPERDMAP)
		return -ENOSPC;

	/* check if the starting leaf indicates that anything
	 * is free.
	 */
	if (leaf[word] == NOFREE)
		return -ENOSPC;

	/* check the dmaps words corresponding to block range to see
	 * if the block range is free.  not all bits of the first and
	 * last words may be contained within the block range.  if this
	 * is the case, we'll work against those words (i.e. partial first
	 * and/or last) on an individual basis (a single pass) and examine
	 * the actual bits to determine if they are free.  a single pass
	 * will be used for all dmap words fully contained within the
	 * specified range.  within this pass, the leaves of the dmap
	 * tree will be examined to determine if the blocks are free. a
	 * single leaf may describe the free space of multiple dmap
	 * words, so we may visit only a subset of the actual leaves
	 * corresponding to the dmap words of the block range.
	 */
	for (rembits = nblocks; rembits > 0; rembits -= nb, dbitno += nb) {
		/* determine the bit number within the word and
		 * the number of bits within the word.
		 */
		wbitno = dbitno & (DBWORD - 1);
		nb = min(rembits, DBWORD - wbitno);

		/* check if only part of the word is to be examined.
		 */
		if (nb < DBWORD) {
			/* check if the bits are free.
			 */
			mask = (ONES << (DBWORD - nb) >> wbitno);
			if ((mask & ~le32_to_cpu(dp->wmap[word])) != mask)
				return -ENOSPC;

			word += 1;
		} else {
			/* one or more dmap words are fully contained
			 * within the block range.  determine how many
			 * words and how many bits.
			 */
			nwords = rembits >> L2DBWORD;
			nb = nwords << L2DBWORD;

			/* now examine the appropriate leaves to determine
			 * if the blocks are free.
			 */
			while (nwords > 0) {
				/* does the leaf describe any free space ?
				 */
				if (leaf[word] < BUDMIN)
					return -ENOSPC;

				/* determine the l2 number of bits provided
				 * by this leaf.
				 */
				l2size =
				    min_t(int, leaf[word], NLSTOL2BSZ(nwords));

				/* determine how many words were handled.
				 */
				nw = BUDSIZE(l2size, BUDMIN);

				nwords -= nw;
				word += nw;
			}
		}
	}

	/* allocate the blocks.
	 */
	return (dbAllocDmap(bmp, dp, blkno, nblocks));
}


/*
 * NAME:	dbAllocNear()
 *
 * FUNCTION:	attempt to allocate a number of contiguous free blocks near
 *		a specified block (hint) within a dmap.
 *
 *		starting with the dmap leaf that covers the hint, we'll
 *		check the next four contiguous leaves for sufficient free
 *		space.  if sufficient free space is found, we'll allocate
 *		the desired free space.
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	dp	-  pointer to dmap.
 *	blkno	-  block number to allocate near.
 *	nblocks	-  actual number of contiguous free blocks desired.
 *	l2nb	-  log2 number of contiguous free blocks desired.
 *	results	-  on successful return, set to the starting block number
 *		   of the newly allocated range.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 *
 * serialization: IREAD_LOCK(ipbmap) held on entry/exit;
 */
static int
dbAllocNear(struct bmap * bmp,
	    struct dmap * dp, s64 blkno, int nblocks, int l2nb, s64 * results)
{
	int word, lword, rc;
	s8 *leaf;

	if (dp->tree.leafidx != cpu_to_le32(LEAFIND)) {
		jfs_error(bmp->db_ipbmap->i_sb, "Corrupt dmap page\n");
		return -EIO;
	}

	leaf = dp->tree.stree + le32_to_cpu(dp->tree.leafidx);

	/* determine the word within the dmap that holds the hint
	 * (i.e. blkno).  also, determine the last word in the dmap
	 * that we'll include in our examination.
	 */
	word = (blkno & (BPERDMAP - 1)) >> L2DBWORD;
	lword = min(word + 4, LPERDMAP);

	/* examine the leaves for sufficient free space.
	 */
	for (; word < lword; word++) {
		/* does the leaf describe sufficient free space ?
		 */
		if (leaf[word] < l2nb)
			continue;

		/* determine the block number within the file system
		 * of the first block described by this dmap word.
		 */
		blkno = le64_to_cpu(dp->start) + (word << L2DBWORD);

		/* if not all bits of the dmap word are free, get the
		 * starting bit number within the dmap word of the required
		 * string of free bits and adjust the block number with the
		 * value.
		 */
		if (leaf[word] < BUDMIN)
			blkno +=
			    dbFindBits(le32_to_cpu(dp->wmap[word]), l2nb);

		/* allocate the blocks.
		 */
		if ((rc = dbAllocDmap(bmp, dp, blkno, nblocks)) == 0)
			*results = blkno;

		return (rc);
	}

	return -ENOSPC;
}


/*
 * NAME:	dbAllocAG()
 *
 * FUNCTION:	attempt to allocate the specified number of contiguous
 *		free blocks within the specified allocation group.
 *
 *		unless the allocation group size is equal to the number
 *		of blocks per dmap, the dmap control pages will be used to
 *		find the required free space, if available.  we start the
 *		search at the highest dmap control page level which
 *		distinctly describes the allocation group's free space
 *		(i.e. the highest level at which the allocation group's
 *		free space is not mixed in with that of any other group).
 *		in addition, we start the search within this level at a
 *		height of the dmapctl dmtree at which the nodes distinctly
 *		describe the allocation group's free space.  at this height,
 *		the allocation group's free space may be represented by 1
 *		or two sub-trees, depending on the allocation group size.
 *		we search the top nodes of these subtrees left to right for
 *		sufficient free space.  if sufficient free space is found,
 *		the subtree is searched to find the leftmost leaf that
 *		has free space.  once we have made it to the leaf, we
 *		move the search to the next lower level dmap control page
 *		corresponding to this leaf.  we continue down the dmap control
 *		pages until we find the dmap that contains or starts the
 *		sufficient free space and we allocate at this dmap.
 *
 *		if the allocation group size is equal to the dmap size,
 *		we'll start at the dmap corresponding to the allocation
 *		group and attempt the allocation at this level.
 *
 *		the dmap control page search is also not performed if the
 *		allocation group is completely free and we go to the first
 *		dmap of the allocation group to do the allocation.  this is
 *		done because the allocation group may be part (not the first
 *		part) of a larger binary buddy system, causing the dmap
 *		control pages to indicate no free space (NOFREE) within
 *		the allocation group.
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	agno	- allocation group number.
 *	nblocks	-  actual number of contiguous free blocks desired.
 *	l2nb	-  log2 number of contiguous free blocks desired.
 *	results	-  on successful return, set to the starting block number
 *		   of the newly allocated range.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 *
 * note: IWRITE_LOCK(ipmap) held on entry/exit;
 */
static int
dbAllocAG(struct bmap * bmp, int agno, s64 nblocks, int l2nb, s64 * results)
{
	struct metapage *mp;
	struct dmapctl *dcp;
	int rc, ti, i, k, m, n, agperlev;
	s64 blkno, lblkno;
	int budmin;

	/* allocation request should not be for more than the
	 * allocation group size.
	 */
	if (l2nb > bmp->db_agl2size) {
		jfs_error(bmp->db_ipbmap->i_sb,
			  "allocation request is larger than the allocation group size\n");
		return -EIO;
	}

	/* determine the starting block number of the allocation
	 * group.
	 */
	blkno = (s64) agno << bmp->db_agl2size;

	/* check if the allocation group size is the minimum allocation
	 * group size or if the allocation group is completely free. if
	 * the allocation group size is the minimum size of BPERDMAP (i.e.
	 * 1 dmap), there is no need to search the dmap control page (below)
	 * that fully describes the allocation group since the allocation
	 * group is already fully described by a dmap.  in this case, we
	 * just call dbAllocCtl() to search the dmap tree and allocate the
	 * required space if available.
	 *
	 * if the allocation group is completely free, dbAllocCtl() is
	 * also called to allocate the required space.  this is done for
	 * two reasons.  first, it makes no sense searching the dmap control
	 * pages for free space when we know that free space exists.  second,
	 * the dmap control pages may indicate that the allocation group
	 * has no free space if the allocation group is part (not the first
	 * part) of a larger binary buddy system.
	 */
	if (bmp->db_agsize == BPERDMAP
	    || bmp->db_agfree[agno] == bmp->db_agsize) {
		rc = dbAllocCtl(bmp, nblocks, l2nb, blkno, results);
		if ((rc == -ENOSPC) &&
		    (bmp->db_agfree[agno] == bmp->db_agsize)) {
			printk(KERN_ERR "blkno = %Lx, blocks = %Lx\n",
			       (unsigned long long) blkno,
			       (unsigned long long) nblocks);
			jfs_error(bmp->db_ipbmap->i_sb,
				  "dbAllocCtl failed in free AG\n");
		}
		return (rc);
	}

	/* the buffer for the dmap control page that fully describes the
	 * allocation group.
	 */
	lblkno = BLKTOCTL(blkno, bmp->db_l2nbperpage, bmp->db_aglevel);
	mp = read_metapage(bmp->db_ipbmap, lblkno, PSIZE, 0);
	if (mp == NULL)
		return -EIO;
	dcp = (struct dmapctl *) mp->data;
	budmin = dcp->budmin;

	if (dcp->leafidx != cpu_to_le32(CTLLEAFIND)) {
		jfs_error(bmp->db_ipbmap->i_sb, "Corrupt dmapctl page\n");
		release_metapage(mp);
		return -EIO;
	}

	/* search the subtree(s) of the dmap control page that describes
	 * the allocation group, looking for sufficient free space.  to begin,
	 * determine how many allocation groups are represented in a dmap
	 * control page at the control page level (i.e. L0, L1, L2) that
	 * fully describes an allocation group. next, determine the starting
	 * tree index of this allocation group within the control page.
	 */
	agperlev =
	    (1 << (L2LPERCTL - (bmp->db_agheight << 1))) / bmp->db_agwidth;
	ti = bmp->db_agstart + bmp->db_agwidth * (agno & (agperlev - 1));

	/* dmap control page trees fan-out by 4 and a single allocation
	 * group may be described by 1 or 2 subtrees within the ag level
	 * dmap control page, depending upon the ag size. examine the ag's
	 * subtrees for sufficient free space, starting with the leftmost
	 * subtree.
	 */
	for (i = 0; i < bmp->db_agwidth; i++, ti++) {
		/* is there sufficient free space ?
		 */
		if (l2nb > dcp->stree[ti])
			continue;

		/* sufficient free space found in a subtree. now search down
		 * the subtree to find the leftmost leaf that describes this
		 * free space.
		 */
		for (k = bmp->db_agheight; k > 0; k--) {
			for (n = 0, m = (ti << 2) + 1; n < 4; n++) {
				if (l2nb <= dcp->stree[m + n]) {
					ti = m + n;
					break;
				}
			}
			if (n == 4) {
				jfs_error(bmp->db_ipbmap->i_sb,
					  "failed descending stree\n");
				release_metapage(mp);
				return -EIO;
			}
		}

		/* determine the block number within the file system
		 * that corresponds to this leaf.
		 */
		if (bmp->db_aglevel == 2)
			blkno = 0;
		else if (bmp->db_aglevel == 1)
			blkno &= ~(MAXL1SIZE - 1);
		else		/* bmp->db_aglevel == 0 */
			blkno &= ~(MAXL0SIZE - 1);

		blkno +=
		    ((s64) (ti - le32_to_cpu(dcp->leafidx))) << budmin;

		/* release the buffer in preparation for going down
		 * the next level of dmap control pages.
		 */
		release_metapage(mp);

		/* check if we need to continue to search down the lower
		 * level dmap control pages.  we need to if the number of
		 * blocks required is less than maximum number of blocks
		 * described at the next lower level.
		 */
		if (l2nb < budmin) {

			/* search the lower level dmap control pages to get
			 * the starting block number of the dmap that
			 * contains or starts off the free space.
			 */
			if ((rc =
			     dbFindCtl(bmp, l2nb, bmp->db_aglevel - 1,
				       &blkno))) {
				if (rc == -ENOSPC) {
					jfs_error(bmp->db_ipbmap->i_sb,
						  "control page inconsistent\n");
					return -EIO;
				}
				return (rc);
			}
		}

		/* allocate the blocks.
		 */
		rc = dbAllocCtl(bmp, nblocks, l2nb, blkno, results);
		if (rc == -ENOSPC) {
			jfs_error(bmp->db_ipbmap->i_sb,
				  "unable to allocate blocks\n");
			rc = -EIO;
		}
		return (rc);
	}

	/* no space in the allocation group.  release the buffer and
	 * return -ENOSPC.
	 */
	release_metapage(mp);

	return -ENOSPC;
}


/*
 * NAME:	dbAllocAny()
 *
 * FUNCTION:	attempt to allocate the specified number of contiguous
 *		free blocks anywhere in the file system.
 *
 *		dbAllocAny() attempts to find the sufficient free space by
 *		searching down the dmap control pages, starting with the
 *		highest level (i.e. L0, L1, L2) control page.  if free space
 *		large enough to satisfy the desired free space is found, the
 *		desired free space is allocated.
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	nblocks	 -  actual number of contiguous free blocks desired.
 *	l2nb	 -  log2 number of contiguous free blocks desired.
 *	results	-  on successful return, set to the starting block number
 *		   of the newly allocated range.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 *
 * serialization: IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static int dbAllocAny(struct bmap * bmp, s64 nblocks, int l2nb, s64 * results)
{
	int rc;
	s64 blkno = 0;

	/* starting with the top level dmap control page, search
	 * down the dmap control levels for sufficient free space.
	 * if free space is found, dbFindCtl() returns the starting
	 * block number of the dmap that contains or starts off the
	 * range of free space.
	 */
	if ((rc = dbFindCtl(bmp, l2nb, bmp->db_maxlevel, &blkno)))
		return (rc);

	/* allocate the blocks.
	 */
	rc = dbAllocCtl(bmp, nblocks, l2nb, blkno, results);
	if (rc == -ENOSPC) {
		jfs_error(bmp->db_ipbmap->i_sb, "unable to allocate blocks\n");
		return -EIO;
	}
	return (rc);
}


/*
 * NAME:	dbDiscardAG()
 *
 * FUNCTION:	attempt to discard (TRIM) all free blocks of specific AG
 *
 *		algorithm:
 *		1) allocate blocks, as large as possible and save them
 *		   while holding IWRITE_LOCK on ipbmap
 *		2) trim all these saved block/length values
 *		3) mark the blocks free again
 *
 *		benefit:
 *		- we work only on one ag at some time, minimizing how long we
 *		  need to lock ipbmap
 *		- reading / writing the fs is possible most time, even on
 *		  trimming
 *
 *		downside:
 *		- we write two times to the dmapctl and dmap pages
 *		- but for me, this seems the best way, better ideas?
 *		/TR 2012
 *
 * PARAMETERS:
 *	ip	- pointer to in-core inode
 *	agno	- ag to trim
 *	minlen	- minimum value of contiguous blocks
 *
 * RETURN VALUES:
 *	s64	- actual number of blocks trimmed
 */
s64 dbDiscardAG(struct inode *ip, int agno, s64 minlen)
{
	struct inode *ipbmap = JFS_SBI(ip->i_sb)->ipbmap;
	struct bmap *bmp = JFS_SBI(ip->i_sb)->bmap;
	s64 nblocks, blkno;
	u64 trimmed = 0;
	int rc, l2nb;
	struct super_block *sb = ipbmap->i_sb;

	struct range2trim {
		u64 blkno;
		u64 nblocks;
	} *totrim, *tt;

	/* max blkno / nblocks pairs to trim */
	int count = 0, range_cnt;
	u64 max_ranges;

	/* prevent others from writing new stuff here, while trimming */
	IWRITE_LOCK(ipbmap, RDWRLOCK_DMAP);

	nblocks = bmp->db_agfree[agno];
	max_ranges = nblocks;
	do_div(max_ranges, minlen);
	range_cnt = min_t(u64, max_ranges + 1, 32 * 1024);
	totrim = kmalloc_array(range_cnt, sizeof(struct range2trim), GFP_NOFS);
	if (totrim == NULL) {
		jfs_error(bmp->db_ipbmap->i_sb, "no memory for trim array\n");
		IWRITE_UNLOCK(ipbmap);
		return 0;
	}

	tt = totrim;
	while (nblocks >= minlen) {
		l2nb = BLKSTOL2(nblocks);

		/* 0 = okay, -EIO = fatal, -ENOSPC -> try smaller block */
		rc = dbAllocAG(bmp, agno, nblocks, l2nb, &blkno);
		if (rc == 0) {
			tt->blkno = blkno;
			tt->nblocks = nblocks;
			tt++; count++;

			/* the whole ag is free, trim now */
			if (bmp->db_agfree[agno] == 0)
				break;

			/* give a hint for the next while */
			nblocks = bmp->db_agfree[agno];
			continue;
		} else if (rc == -ENOSPC) {
			/* search for next smaller log2 block */
			l2nb = BLKSTOL2(nblocks) - 1;
			nblocks = 1LL << l2nb;
		} else {
			/* Trim any already allocated blocks */
			jfs_error(bmp->db_ipbmap->i_sb, "-EIO\n");
			break;
		}

		/* check, if our trim array is full */
		if (unlikely(count >= range_cnt - 1))
			break;
	}
	IWRITE_UNLOCK(ipbmap);

	tt->nblocks = 0; /* mark the current end */
	for (tt = totrim; tt->nblocks != 0; tt++) {
		/* when mounted with online discard, dbFree() will
		 * call jfs_issue_discard() itself */
		if (!(JFS_SBI(sb)->flag & JFS_DISCARD))
			jfs_issue_discard(ip, tt->blkno, tt->nblocks);
		dbFree(ip, tt->blkno, tt->nblocks);
		trimmed += tt->nblocks;
	}
	kfree(totrim);

	return trimmed;
}

/*
 * NAME:	dbFindCtl()
 *
 * FUNCTION:	starting at a specified dmap control page level and block
 *		number, search down the dmap control levels for a range of
 *		contiguous free blocks large enough to satisfy an allocation
 *		request for the specified number of free blocks.
 *
 *		if sufficient contiguous free blocks are found, this routine
 *		returns the starting block number within a dmap page that
 *		contains or starts a range of contiqious free blocks that
 *		is sufficient in size.
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	level	-  starting dmap control page level.
 *	l2nb	-  log2 number of contiguous free blocks desired.
 *	*blkno	-  on entry, starting block number for conducting the search.
 *		   on successful return, the first block within a dmap page
 *		   that contains or starts a range of contiguous free blocks.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 *
 * serialization: IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static int dbFindCtl(struct bmap * bmp, int l2nb, int level, s64 * blkno)
{
	int rc, leafidx, lev;
	s64 b, lblkno;
	struct dmapctl *dcp;
	int budmin;
	struct metapage *mp;

	/* starting at the specified dmap control page level and block
	 * number, search down the dmap control levels for the starting
	 * block number of a dmap page that contains or starts off
	 * sufficient free blocks.
	 */
	for (lev = level, b = *blkno; lev >= 0; lev--) {
		/* get the buffer of the dmap control page for the block
		 * number and level (i.e. L0, L1, L2).
		 */
		lblkno = BLKTOCTL(b, bmp->db_l2nbperpage, lev);
		mp = read_metapage(bmp->db_ipbmap, lblkno, PSIZE, 0);
		if (mp == NULL)
			return -EIO;
		dcp = (struct dmapctl *) mp->data;
		budmin = dcp->budmin;

		if (dcp->leafidx != cpu_to_le32(CTLLEAFIND)) {
			jfs_error(bmp->db_ipbmap->i_sb,
				  "Corrupt dmapctl page\n");
			release_metapage(mp);
			return -EIO;
		}

		/* search the tree within the dmap control page for
		 * sufficient free space.  if sufficient free space is found,
		 * dbFindLeaf() returns the index of the leaf at which
		 * free space was found.
		 */
		rc = dbFindLeaf((dmtree_t *) dcp, l2nb, &leafidx);

		/* release the buffer.
		 */
		release_metapage(mp);

		/* space found ?
		 */
		if (rc) {
			if (lev != level) {
				jfs_error(bmp->db_ipbmap->i_sb,
					  "dmap inconsistent\n");
				return -EIO;
			}
			return -ENOSPC;
		}

		/* adjust the block number to reflect the location within
		 * the dmap control page (i.e. the leaf) at which free
		 * space was found.
		 */
		b += (((s64) leafidx) << budmin);

		/* we stop the search at this dmap control page level if
		 * the number of blocks required is greater than or equal
		 * to the maximum number of blocks described at the next
		 * (lower) level.
		 */
		if (l2nb >= budmin)
			break;
	}

	*blkno = b;
	return (0);
}


/*
 * NAME:	dbAllocCtl()
 *
 * FUNCTION:	attempt to allocate a specified number of contiguous
 *		blocks starting within a specific dmap.
 *
 *		this routine is called by higher level routines that search
 *		the dmap control pages above the actual dmaps for contiguous
 *		free space.  the result of successful searches by these
 *		routines are the starting block numbers within dmaps, with
 *		the dmaps themselves containing the desired contiguous free
 *		space or starting a contiguous free space of desired size
 *		that is made up of the blocks of one or more dmaps. these
 *		calls should not fail due to insufficent resources.
 *
 *		this routine is called in some cases where it is not known
 *		whether it will fail due to insufficient resources.  more
 *		specifically, this occurs when allocating from an allocation
 *		group whose size is equal to the number of blocks per dmap.
 *		in this case, the dmap control pages are not examined prior
 *		to calling this routine (to save pathlength) and the call
 *		might fail.
 *
 *		for a request size that fits within a dmap, this routine relies
 *		upon the dmap's dmtree to find the requested contiguous free
 *		space.  for request sizes that are larger than a dmap, the
 *		requested free space will start at the first block of the
 *		first dmap (i.e. blkno).
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	nblocks	 -  actual number of contiguous free blocks to allocate.
 *	l2nb	 -  log2 number of contiguous free blocks to allocate.
 *	blkno	 -  starting block number of the dmap to start the allocation
 *		    from.
 *	results	-  on successful return, set to the starting block number
 *		   of the newly allocated range.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 *
 * serialization: IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static int
dbAllocCtl(struct bmap * bmp, s64 nblocks, int l2nb, s64 blkno, s64 * results)
{
	int rc, nb;
	s64 b, lblkno, n;
	struct metapage *mp;
	struct dmap *dp;

	/* check if the allocation request is confined to a single dmap.
	 */
	if (l2nb <= L2BPERDMAP) {
		/* get the buffer for the dmap.
		 */
		lblkno = BLKTODMAP(blkno, bmp->db_l2nbperpage);
		mp = read_metapage(bmp->db_ipbmap, lblkno, PSIZE, 0);
		if (mp == NULL)
			return -EIO;
		dp = (struct dmap *) mp->data;

		/* try to allocate the blocks.
		 */
		rc = dbAllocDmapLev(bmp, dp, (int) nblocks, l2nb, results);
		if (rc == 0)
			mark_metapage_dirty(mp);

		release_metapage(mp);

		return (rc);
	}

	/* allocation request involving multiple dmaps. it must start on
	 * a dmap boundary.
	 */
	assert((blkno & (BPERDMAP - 1)) == 0);

	/* allocate the blocks dmap by dmap.
	 */
	for (n = nblocks, b = blkno; n > 0; n -= nb, b += nb) {
		/* get the buffer for the dmap.
		 */
		lblkno = BLKTODMAP(b, bmp->db_l2nbperpage);
		mp = read_metapage(bmp->db_ipbmap, lblkno, PSIZE, 0);
		if (mp == NULL) {
			rc = -EIO;
			goto backout;
		}
		dp = (struct dmap *) mp->data;

		/* the dmap better be all free.
		 */
		if (dp->tree.stree[ROOT] != L2BPERDMAP) {
			release_metapage(mp);
			jfs_error(bmp->db_ipbmap->i_sb,
				  "the dmap is not all free\n");
			rc = -EIO;
			goto backout;
		}

		/* determine how many blocks to allocate from this dmap.
		 */
		nb = min_t(s64, n, BPERDMAP);

		/* allocate the blocks from the dmap.
		 */
		if ((rc = dbAllocDmap(bmp, dp, b, nb))) {
			release_metapage(mp);
			goto backout;
		}

		/* write the buffer.
		 */
		write_metapage(mp);
	}

	/* set the results (starting block number) and return.
	 */
	*results = blkno;
	return (0);

	/* something failed in handling an allocation request involving
	 * multiple dmaps.  we'll try to clean up by backing out any
	 * allocation that has already happened for this request.  if
	 * we fail in backing out the allocation, we'll mark the file
	 * system to indicate that blocks have been leaked.
	 */
      backout:

	/* try to backout the allocations dmap by dmap.
	 */
	for (n = nblocks - n, b = blkno; n > 0;
	     n -= BPERDMAP, b += BPERDMAP) {
		/* get the buffer for this dmap.
		 */
		lblkno = BLKTODMAP(b, bmp->db_l2nbperpage);
		mp = read_metapage(bmp->db_ipbmap, lblkno, PSIZE, 0);
		if (mp == NULL) {
			/* could not back out.  mark the file system
			 * to indicate that we have leaked blocks.
			 */
			jfs_error(bmp->db_ipbmap->i_sb,
				  "I/O Error: Block Leakage\n");
			continue;
		}
		dp = (struct dmap *) mp->data;

		/* free the blocks is this dmap.
		 */
		if (dbFreeDmap(bmp, dp, b, BPERDMAP)) {
			/* could not back out.  mark the file system
			 * to indicate that we have leaked blocks.
			 */
			release_metapage(mp);
			jfs_error(bmp->db_ipbmap->i_sb, "Block Leakage\n");
			continue;
		}

		/* write the buffer.
		 */
		write_metapage(mp);
	}

	return (rc);
}


/*
 * NAME:	dbAllocDmapLev()
 *
 * FUNCTION:	attempt to allocate a specified number of contiguous blocks
 *		from a specified dmap.
 *
 *		this routine checks if the contiguous blocks are available.
 *		if so, nblocks of blocks are allocated; otherwise, ENOSPC is
 *		returned.
 *
 * PARAMETERS:
 *	mp	-  pointer to bmap descriptor
 *	dp	-  pointer to dmap to attempt to allocate blocks from.
 *	l2nb	-  log2 number of contiguous block desired.
 *	nblocks	-  actual number of contiguous block desired.
 *	results	-  on successful return, set to the starting block number
 *		   of the newly allocated range.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient disk resources
 *	-EIO	- i/o error
 *
 * serialization: IREAD_LOCK(ipbmap), e.g., from dbAlloc(), or
 *	IWRITE_LOCK(ipbmap), e.g., dbAllocCtl(), held on entry/exit;
 */
static int
dbAllocDmapLev(struct bmap * bmp,
	       struct dmap * dp, int nblocks, int l2nb, s64 * results)
{
	s64 blkno;
	int leafidx, rc;

	/* can't be more than a dmaps worth of blocks */
	assert(l2nb <= L2BPERDMAP);

	/* search the tree within the dmap page for sufficient
	 * free space.  if sufficient free space is found, dbFindLeaf()
	 * returns the index of the leaf at which free space was found.
	 */
	if (dbFindLeaf((dmtree_t *) & dp->tree, l2nb, &leafidx))
		return -ENOSPC;

	/* determine the block number within the file system corresponding
	 * to the leaf at which free space was found.
	 */
	blkno = le64_to_cpu(dp->start) + (leafidx << L2DBWORD);

	/* if not all bits of the dmap word are free, get the starting
	 * bit number within the dmap word of the required string of free
	 * bits and adjust the block number with this value.
	 */
	if (dp->tree.stree[leafidx + LEAFIND] < BUDMIN)
		blkno += dbFindBits(le32_to_cpu(dp->wmap[leafidx]), l2nb);

	/* allocate the blocks */
	if ((rc = dbAllocDmap(bmp, dp, blkno, nblocks)) == 0)
		*results = blkno;

	return (rc);
}


/*
 * NAME:	dbAllocDmap()
 *
 * FUNCTION:	adjust the disk allocation map to reflect the allocation
 *		of a specified block range within a dmap.
 *
 *		this routine allocates the specified blocks from the dmap
 *		through a call to dbAllocBits(). if the allocation of the
 *		block range causes the maximum string of free blocks within
 *		the dmap to change (i.e. the value of the root of the dmap's
 *		dmtree), this routine will cause this change to be reflected
 *		up through the appropriate levels of the dmap control pages
 *		by a call to dbAdjCtl() for the L0 dmap control page that
 *		covers this dmap.
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	dp	-  pointer to dmap to allocate the block range from.
 *	blkno	-  starting block number of the block to be allocated.
 *	nblocks	-  number of blocks to be allocated.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error
 *
 * serialization: IREAD_LOCK(ipbmap) or IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static int dbAllocDmap(struct bmap * bmp, struct dmap * dp, s64 blkno,
		       int nblocks)
{
	s8 oldroot;
	int rc;

	/* save the current value of the root (i.e. maximum free string)
	 * of the dmap tree.
	 */
	oldroot = dp->tree.stree[ROOT];

	/* allocate the specified (blocks) bits */
	dbAllocBits(bmp, dp, blkno, nblocks);

	/* if the root has not changed, done. */
	if (dp->tree.stree[ROOT] == oldroot)
		return (0);

	/* root changed. bubble the change up to the dmap control pages.
	 * if the adjustment of the upper level control pages fails,
	 * backout the bit allocation (thus making everything consistent).
	 */
	if ((rc = dbAdjCtl(bmp, blkno, dp->tree.stree[ROOT], 1, 0)))
		dbFreeBits(bmp, dp, blkno, nblocks);

	return (rc);
}


/*
 * NAME:	dbFreeDmap()
 *
 * FUNCTION:	adjust the disk allocation map to reflect the allocation
 *		of a specified block range within a dmap.
 *
 *		this routine frees the specified blocks from the dmap through
 *		a call to dbFreeBits(). if the deallocation of the block range
 *		causes the maximum string of free blocks within the dmap to
 *		change (i.e. the value of the root of the dmap's dmtree), this
 *		routine will cause this change to be reflected up through the
 *		appropriate levels of the dmap control pages by a call to
 *		dbAdjCtl() for the L0 dmap control page that covers this dmap.
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	dp	-  pointer to dmap to free the block range from.
 *	blkno	-  starting block number of the block to be freed.
 *	nblocks	-  number of blocks to be freed.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error
 *
 * serialization: IREAD_LOCK(ipbmap) or IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static int dbFreeDmap(struct bmap * bmp, struct dmap * dp, s64 blkno,
		      int nblocks)
{
	s8 oldroot;
	int rc = 0, word;

	/* save the current value of the root (i.e. maximum free string)
	 * of the dmap tree.
	 */
	oldroot = dp->tree.stree[ROOT];

	/* free the specified (blocks) bits */
	rc = dbFreeBits(bmp, dp, blkno, nblocks);

	/* if error or the root has not changed, done. */
	if (rc || (dp->tree.stree[ROOT] == oldroot))
		return (rc);

	/* root changed. bubble the change up to the dmap control pages.
	 * if the adjustment of the upper level control pages fails,
	 * backout the deallocation.
	 */
	if ((rc = dbAdjCtl(bmp, blkno, dp->tree.stree[ROOT], 0, 0))) {
		word = (blkno & (BPERDMAP - 1)) >> L2DBWORD;

		/* as part of backing out the deallocation, we will have
		 * to back split the dmap tree if the deallocation caused
		 * the freed blocks to become part of a larger binary buddy
		 * system.
		 */
		if (dp->tree.stree[word] == NOFREE)
			dbBackSplit((dmtree_t *) & dp->tree, word);

		dbAllocBits(bmp, dp, blkno, nblocks);
	}

	return (rc);
}


/*
 * NAME:	dbAllocBits()
 *
 * FUNCTION:	allocate a specified block range from a dmap.
 *
 *		this routine updates the dmap to reflect the working
 *		state allocation of the specified block range. it directly
 *		updates the bits of the working map and causes the adjustment
 *		of the binary buddy system described by the dmap's dmtree
 *		leaves to reflect the bits allocated.  it also causes the
 *		dmap's dmtree, as a whole, to reflect the allocated range.
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	dp	-  pointer to dmap to allocate bits from.
 *	blkno	-  starting block number of the bits to be allocated.
 *	nblocks	-  number of bits to be allocated.
 *
 * RETURN VALUES: none
 *
 * serialization: IREAD_LOCK(ipbmap) or IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static void dbAllocBits(struct bmap * bmp, struct dmap * dp, s64 blkno,
			int nblocks)
{
	int dbitno, word, rembits, nb, nwords, wbitno, nw, agno;
	dmtree_t *tp = (dmtree_t *) & dp->tree;
	int size;
	s8 *leaf;

	/* pick up a pointer to the leaves of the dmap tree */
	leaf = dp->tree.stree + LEAFIND;

	/* determine the bit number and word within the dmap of the
	 * starting block.
	 */
	dbitno = blkno & (BPERDMAP - 1);
	word = dbitno >> L2DBWORD;

	/* block range better be within the dmap */
	assert(dbitno + nblocks <= BPERDMAP);

	/* allocate the bits of the dmap's words corresponding to the block
	 * range. not all bits of the first and last words may be contained
	 * within the block range.  if this is the case, we'll work against
	 * those words (i.e. partial first and/or last) on an individual basis
	 * (a single pass), allocating the bits of interest by hand and
	 * updating the leaf corresponding to the dmap word. a single pass
	 * will be used for all dmap words fully contained within the
	 * specified range.  within this pass, the bits of all fully contained
	 * dmap words will be marked as free in a single shot and the leaves
	 * will be updated. a single leaf may describe the free space of
	 * multiple dmap words, so we may update only a subset of the actual
	 * leaves corresponding to the dmap words of the block range.
	 */
	for (rembits = nblocks; rembits > 0; rembits -= nb, dbitno += nb) {
		/* determine the bit number within the word and
		 * the number of bits within the word.
		 */
		wbitno = dbitno & (DBWORD - 1);
		nb = min(rembits, DBWORD - wbitno);

		/* check if only part of a word is to be allocated.
		 */
		if (nb < DBWORD) {
			/* allocate (set to 1) the appropriate bits within
			 * this dmap word.
			 */
			dp->wmap[word] |= cpu_to_le32(ONES << (DBWORD - nb)
						      >> wbitno);

			/* update the leaf for this dmap word. in addition
			 * to setting the leaf value to the binary buddy max
			 * of the updated dmap word, dbSplit() will split
			 * the binary system of the leaves if need be.
			 */
			dbSplit(tp, word, BUDMIN,
				dbMaxBud((u8 *) & dp->wmap[word]));

			word += 1;
		} else {
			/* one or more dmap words are fully contained
			 * within the block range.  determine how many
			 * words and allocate (set to 1) the bits of these
			 * words.
			 */
			nwords = rembits >> L2DBWORD;
			memset(&dp->wmap[word], (int) ONES, nwords * 4);

			/* determine how many bits.
			 */
			nb = nwords << L2DBWORD;

			/* now update the appropriate leaves to reflect
			 * the allocated words.
			 */
			for (; nwords > 0; nwords -= nw) {
				if (leaf[word] < BUDMIN) {
					jfs_error(bmp->db_ipbmap->i_sb,
						  "leaf page corrupt\n");
					break;
				}

				/* determine what the leaf value should be
				 * updated to as the minimum of the l2 number
				 * of bits being allocated and the l2 number
				 * of bits currently described by this leaf.
				 */
				size = min_t(int, leaf[word],
					     NLSTOL2BSZ(nwords));

				/* update the leaf to reflect the allocation.
				 * in addition to setting the leaf value to
				 * NOFREE, dbSplit() will split the binary
				 * system of the leaves to reflect the current
				 * allocation (size).
				 */
				dbSplit(tp, word, size, NOFREE);

				/* get the number of dmap words handled */
				nw = BUDSIZE(size, BUDMIN);
				word += nw;
			}
		}
	}

	/* update the free count for this dmap */
	le32_add_cpu(&dp->nfree, -nblocks);

	BMAP_LOCK(bmp);

	/* if this allocation group is completely free,
	 * update the maximum allocation group number if this allocation
	 * group is the new max.
	 */
	agno = blkno >> bmp->db_agl2size;
	if (agno > bmp->db_maxag)
		bmp->db_maxag = agno;

	/* update the free count for the allocation group and map */
	bmp->db_agfree[agno] -= nblocks;
	bmp->db_nfree -= nblocks;

	BMAP_UNLOCK(bmp);
}


/*
 * NAME:	dbFreeBits()
 *
 * FUNCTION:	free a specified block range from a dmap.
 *
 *		this routine updates the dmap to reflect the working
 *		state allocation of the specified block range. it directly
 *		updates the bits of the working map and causes the adjustment
 *		of the binary buddy system described by the dmap's dmtree
 *		leaves to reflect the bits freed.  it also causes the dmap's
 *		dmtree, as a whole, to reflect the deallocated range.
 *
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	dp	-  pointer to dmap to free bits from.
 *	blkno	-  starting block number of the bits to be freed.
 *	nblocks	-  number of bits to be freed.
 *
 * RETURN VALUES: 0 for success
 *
 * serialization: IREAD_LOCK(ipbmap) or IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static int dbFreeBits(struct bmap * bmp, struct dmap * dp, s64 blkno,
		       int nblocks)
{
	int dbitno, word, rembits, nb, nwords, wbitno, nw, agno;
	dmtree_t *tp = (dmtree_t *) & dp->tree;
	int rc = 0;
	int size;

	/* determine the bit number and word within the dmap of the
	 * starting block.
	 */
	dbitno = blkno & (BPERDMAP - 1);
	word = dbitno >> L2DBWORD;

	/* block range better be within the dmap.
	 */
	assert(dbitno + nblocks <= BPERDMAP);

	/* free the bits of the dmaps words corresponding to the block range.
	 * not all bits of the first and last words may be contained within
	 * the block range.  if this is the case, we'll work against those
	 * words (i.e. partial first and/or last) on an individual basis
	 * (a single pass), freeing the bits of interest by hand and updating
	 * the leaf corresponding to the dmap word. a single pass will be used
	 * for all dmap words fully contained within the specified range.
	 * within this pass, the bits of all fully contained dmap words will
	 * be marked as free in a single shot and the leaves will be updated. a
	 * single leaf may describe the free space of multiple dmap words,
	 * so we may update only a subset of the actual leaves corresponding
	 * to the dmap words of the block range.
	 *
	 * dbJoin() is used to update leaf values and will join the binary
	 * buddy system of the leaves if the new leaf values indicate this
	 * should be done.
	 */
	for (rembits = nblocks; rembits > 0; rembits -= nb, dbitno += nb) {
		/* determine the bit number within the word and
		 * the number of bits within the word.
		 */
		wbitno = dbitno & (DBWORD - 1);
		nb = min(rembits, DBWORD - wbitno);

		/* check if only part of a word is to be freed.
		 */
		if (nb < DBWORD) {
			/* free (zero) the appropriate bits within this
			 * dmap word.
			 */
			dp->wmap[word] &=
			    cpu_to_le32(~(ONES << (DBWORD - nb)
					  >> wbitno));

			/* update the leaf for this dmap word.
			 */
			rc = dbJoin(tp, word,
				    dbMaxBud((u8 *) & dp->wmap[word]));
			if (rc)
				return rc;

			word += 1;
		} else {
			/* one or more dmap words are fully contained
			 * within the block range.  determine how many
			 * words and free (zero) the bits of these words.
			 */
			nwords = rembits >> L2DBWORD;
			memset(&dp->wmap[word], 0, nwords * 4);

			/* determine how many bits.
			 */
			nb = nwords << L2DBWORD;

			/* now update the appropriate leaves to reflect
			 * the freed words.
			 */
			for (; nwords > 0; nwords -= nw) {
				/* determine what the leaf value should be
				 * updated to as the minimum of the l2 number
				 * of bits being freed and the l2 (max) number
				 * of bits that can be described by this leaf.
				 */
				size =
				    min(LITOL2BSZ
					(word, L2LPERDMAP, BUDMIN),
					NLSTOL2BSZ(nwords));

				/* update the leaf.
				 */
				rc = dbJoin(tp, word, size);
				if (rc)
					return rc;

				/* get the number of dmap words handled.
				 */
				nw = BUDSIZE(size, BUDMIN);
				word += nw;
			}
		}
	}

	/* update the free count for this dmap.
	 */
	le32_add_cpu(&dp->nfree, nblocks);

	BMAP_LOCK(bmp);

	/* update the free count for the allocation group and
	 * map.
	 */
	agno = blkno >> bmp->db_agl2size;
	bmp->db_nfree += nblocks;
	bmp->db_agfree[agno] += nblocks;

	/* check if this allocation group is not completely free and
	 * if it is currently the maximum (rightmost) allocation group.
	 * if so, establish the new maximum allocation group number by
	 * searching left for the first allocation group with allocation.
	 */
	if ((bmp->db_agfree[agno] == bmp->db_agsize && agno == bmp->db_maxag) ||
	    (agno == bmp->db_numag - 1 &&
	     bmp->db_agfree[agno] == (bmp-> db_mapsize & (BPERDMAP - 1)))) {
		while (bmp->db_maxag > 0) {
			bmp->db_maxag -= 1;
			if (bmp->db_agfree[bmp->db_maxag] !=
			    bmp->db_agsize)
				break;
		}

		/* re-establish the allocation group preference if the
		 * current preference is right of the maximum allocation
		 * group.
		 */
		if (bmp->db_agpref > bmp->db_maxag)
			bmp->db_agpref = bmp->db_maxag;
	}

	BMAP_UNLOCK(bmp);

	return 0;
}


/*
 * NAME:	dbAdjCtl()
 *
 * FUNCTION:	adjust a dmap control page at a specified level to reflect
 *		the change in a lower level dmap or dmap control page's
 *		maximum string of free blocks (i.e. a change in the root
 *		of the lower level object's dmtree) due to the allocation
 *		or deallocation of a range of blocks with a single dmap.
 *
 *		on entry, this routine is provided with the new value of
 *		the lower level dmap or dmap control page root and the
 *		starting block number of the block range whose allocation
 *		or deallocation resulted in the root change.  this range
 *		is respresented by a single leaf of the current dmapctl
 *		and the leaf will be updated with this value, possibly
 *		causing a binary buddy system within the leaves to be
 *		split or joined.  the update may also cause the dmapctl's
 *		dmtree to be updated.
 *
 *		if the adjustment of the dmap control page, itself, causes its
 *		root to change, this change will be bubbled up to the next dmap
 *		control level by a recursive call to this routine, specifying
 *		the new root value and the next dmap control page level to
 *		be adjusted.
 * PARAMETERS:
 *	bmp	-  pointer to bmap descriptor
 *	blkno	-  the first block of a block range within a dmap.  it is
 *		   the allocation or deallocation of this block range that
 *		   requires the dmap control page to be adjusted.
 *	newval	-  the new value of the lower level dmap or dmap control
 *		   page root.
 *	alloc	-  'true' if adjustment is due to an allocation.
 *	level	-  current level of dmap control page (i.e. L0, L1, L2) to
 *		   be adjusted.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error
 *
 * serialization: IREAD_LOCK(ipbmap) or IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static int
dbAdjCtl(struct bmap * bmp, s64 blkno, int newval, int alloc, int level)
{
	struct metapage *mp;
	s8 oldroot;
	int oldval;
	s64 lblkno;
	struct dmapctl *dcp;
	int rc, leafno, ti;

	/* get the buffer for the dmap control page for the specified
	 * block number and control page level.
	 */
	lblkno = BLKTOCTL(blkno, bmp->db_l2nbperpage, level);
	mp = read_metapage(bmp->db_ipbmap, lblkno, PSIZE, 0);
	if (mp == NULL)
		return -EIO;
	dcp = (struct dmapctl *) mp->data;

	if (dcp->leafidx != cpu_to_le32(CTLLEAFIND)) {
		jfs_error(bmp->db_ipbmap->i_sb, "Corrupt dmapctl page\n");
		release_metapage(mp);
		return -EIO;
	}

	/* determine the leaf number corresponding to the block and
	 * the index within the dmap control tree.
	 */
	leafno = BLKTOCTLLEAF(blkno, dcp->budmin);
	ti = leafno + le32_to_cpu(dcp->leafidx);

	/* save the current leaf value and the current root level (i.e.
	 * maximum l2 free string described by this dmapctl).
	 */
	oldval = dcp->stree[ti];
	oldroot = dcp->stree[ROOT];

	/* check if this is a control page update for an allocation.
	 * if so, update the leaf to reflect the new leaf value using
	 * dbSplit(); otherwise (deallocation), use dbJoin() to update
	 * the leaf with the new value.  in addition to updating the
	 * leaf, dbSplit() will also split the binary buddy system of
	 * the leaves, if required, and bubble new values within the
	 * dmapctl tree, if required.  similarly, dbJoin() will join
	 * the binary buddy system of leaves and bubble new values up
	 * the dmapctl tree as required by the new leaf value.
	 */
	if (alloc) {
		/* check if we are in the middle of a binary buddy
		 * system.  this happens when we are performing the
		 * first allocation out of an allocation group that
		 * is part (not the first part) of a larger binary
		 * buddy system.  if we are in the middle, back split
		 * the system prior to calling dbSplit() which assumes
		 * that it is at the front of a binary buddy system.
		 */
		if (oldval == NOFREE) {
			rc = dbBackSplit((dmtree_t *) dcp, leafno);
			if (rc) {
				release_metapage(mp);
				return rc;
			}
			oldval = dcp->stree[ti];
		}
		dbSplit((dmtree_t *) dcp, leafno, dcp->budmin, newval);
	} else {
		rc = dbJoin((dmtree_t *) dcp, leafno, newval);
		if (rc) {
			release_metapage(mp);
			return rc;
		}
	}

	/* check if the root of the current dmap control page changed due
	 * to the update and if the current dmap control page is not at
	 * the current top level (i.e. L0, L1, L2) of the map.  if so (i.e.
	 * root changed and this is not the top level), call this routine
	 * again (recursion) for the next higher level of the mapping to
	 * reflect the change in root for the current dmap control page.
	 */
	if (dcp->stree[ROOT] != oldroot) {
		/* are we below the top level of the map.  if so,
		 * bubble the root up to the next higher level.
		 */
		if (level < bmp->db_maxlevel) {
			/* bubble up the new root of this dmap control page to
			 * the next level.
			 */
			if ((rc =
			     dbAdjCtl(bmp, blkno, dcp->stree[ROOT], alloc,
				      level + 1))) {
				/* something went wrong in bubbling up the new
				 * root value, so backout the changes to the
				 * current dmap control page.
				 */
				if (alloc) {
					dbJoin((dmtree_t *) dcp, leafno,
					       oldval);
				} else {
					/* the dbJoin() above might have
					 * caused a larger binary buddy system
					 * to form and we may now be in the
					 * middle of it.  if this is the case,
					 * back split the buddies.
					 */
					if (dcp->stree[ti] == NOFREE)
						dbBackSplit((dmtree_t *)
							    dcp, leafno);
					dbSplit((dmtree_t *) dcp, leafno,
						dcp->budmin, oldval);
				}

				/* release the buffer and return the error.
				 */
				release_metapage(mp);
				return (rc);
			}
		} else {
			/* we're at the top level of the map. update
			 * the bmap control page to reflect the size
			 * of the maximum free buddy system.
			 */
			assert(level == bmp->db_maxlevel);
			if (bmp->db_maxfreebud != oldroot) {
				jfs_error(bmp->db_ipbmap->i_sb,
					  "the maximum free buddy is not the old root\n");
			}
			bmp->db_maxfreebud = dcp->stree[ROOT];
		}
	}

	/* write the buffer.
	 */
	write_metapage(mp);

	return (0);
}


/*
 * NAME:	dbSplit()
 *
 * FUNCTION:	update the leaf of a dmtree with a new value, splitting
 *		the leaf from the binary buddy system of the dmtree's
 *		leaves, as required.
 *
 * PARAMETERS:
 *	tp	- pointer to the tree containing the leaf.
 *	leafno	- the number of the leaf to be updated.
 *	splitsz	- the size the binary buddy system starting at the leaf
 *		  must be split to, specified as the log2 number of blocks.
 *	newval	- the new value for the leaf.
 *
 * RETURN VALUES: none
 *
 * serialization: IREAD_LOCK(ipbmap) or IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static void dbSplit(dmtree_t * tp, int leafno, int splitsz, int newval)
{
	int budsz;
	int cursz;
	s8 *leaf = tp->dmt_stree + le32_to_cpu(tp->dmt_leafidx);

	/* check if the leaf needs to be split.
	 */
	if (leaf[leafno] > tp->dmt_budmin) {
		/* the split occurs by cutting the buddy system in half
		 * at the specified leaf until we reach the specified
		 * size.  pick up the starting split size (current size
		 * - 1 in l2) and the corresponding buddy size.
		 */
		cursz = leaf[leafno] - 1;
		budsz = BUDSIZE(cursz, tp->dmt_budmin);

		/* split until we reach the specified size.
		 */
		while (cursz >= splitsz) {
			/* update the buddy's leaf with its new value.
			 */
			dbAdjTree(tp, leafno ^ budsz, cursz);

			/* on to the next size and buddy.
			 */
			cursz -= 1;
			budsz >>= 1;
		}
	}

	/* adjust the dmap tree to reflect the specified leaf's new
	 * value.
	 */
	dbAdjTree(tp, leafno, newval);
}


/*
 * NAME:	dbBackSplit()
 *
 * FUNCTION:	back split the binary buddy system of dmtree leaves
 *		that hold a specified leaf until the specified leaf
 *		starts its own binary buddy system.
 *
 *		the allocators typically perform allocations at the start
 *		of binary buddy systems and dbSplit() is used to accomplish
 *		any required splits.  in some cases, however, allocation
 *		may occur in the middle of a binary system and requires a
 *		back split, with the split proceeding out from the middle of
 *		the system (less efficient) rather than the start of the
 *		system (more efficient).  the cases in which a back split
 *		is required are rare and are limited to the first allocation
 *		within an allocation group which is a part (not first part)
 *		of a larger binary buddy system and a few exception cases
 *		in which a previous join operation must be backed out.
 *
 * PARAMETERS:
 *	tp	- pointer to the tree containing the leaf.
 *	leafno	- the number of the leaf to be updated.
 *
 * RETURN VALUES: none
 *
 * serialization: IREAD_LOCK(ipbmap) or IWRITE_LOCK(ipbmap) held on entry/exit;
 */
static int dbBackSplit(dmtree_t * tp, int leafno)
{
	int budsz, bud, w, bsz, size;
	int cursz;
	s8 *leaf = tp->dmt_stree + le32_to_cpu(tp->dmt_leafidx);

	/* leaf should be part (not first part) of a binary
	 * buddy system.
	 */
	assert(leaf[leafno] == NOFREE);

	/* the back split is accomplished by iteratively finding the leaf
	 * that starts the buddy system that contains the specified leaf and
	 * splitting that system in two.  this iteration continues until
	 * the specified leaf becomes the start of a buddy system.
	 *
	 * determine maximum possible l2 size for the specified leaf.
	 */
	size =
	    LITOL2BSZ(leafno, le32_to_cpu(tp->dmt_l2nleafs),
		      tp->dmt_budmin);

	/* determine the number of leaves covered by this size.  this
	 * is the buddy size that we will start with as we search for
	 * the buddy system that contains the specified leaf.
	 */
	budsz = BUDSIZE(size, tp->dmt_budmin);

	/* back split.
	 */
	while (leaf[leafno] == NOFREE) {
		/* find the leftmost buddy leaf.
		 */
		for (w = leafno, bsz = budsz;; bsz <<= 1,
		     w = (w < bud) ? w : bud) {
			if (bsz >= le32_to_cpu(tp->dmt_nleafs)) {
				jfs_err("JFS: block map error in dbBackSplit");
				return -EIO;
			}

			/* determine the buddy.
			 */
			bud = w ^ bsz;

			/* check if this buddy is the start of the system.
			 */
			if (leaf[bud] != NOFREE) {
				/* split the leaf at the start of the
				 * system in two.
				 */
				cursz = leaf[bud] - 1;
				dbSplit(tp, bud, cursz, cursz);
				break;
			}
		}
	}

	if (leaf[leafno] != size) {
		jfs_err("JFS: wrong leaf value in dbBackSplit");
		return -EIO;
	}
	return 0;
}


/*
 * NAME:	dbJoin()
 *
 * FUNCTION:	update the leaf of a dmtree with a new value, joining
 *		the leaf with other leaves of the dmtree into a multi-leaf
 *		binary buddy system, as required.
 *
 * PARAMETERS:
 *	tp	- pointer to the tree containing the leaf.
 *	leafno	- the number of the leaf to be updated.
 *	newval	- the new value for the leaf.
 *
 * RETURN VALUES: none
 */
static int dbJoin(dmtree_t * tp, int leafno, int newval)
{
	int budsz, buddy;
	s8 *leaf;

	/* can the new leaf value require a join with other leaves ?
	 */
	if (newval >= tp->dmt_budmin) {
		/* pickup a pointer to the leaves of the tree.
		 */
		leaf = tp->dmt_stree + le32_to_cpu(tp->dmt_leafidx);

		/* try to join the specified leaf into a large binary
		 * buddy system.  the join proceeds by attempting to join
		 * the specified leafno with its buddy (leaf) at new value.
		 * if the join occurs, we attempt to join the left leaf
		 * of the joined buddies with its buddy at new value + 1.
		 * we continue to join until we find a buddy that cannot be
		 * joined (does not have a value equal to the size of the
		 * last join) or until all leaves have been joined into a
		 * single system.
		 *
		 * get the buddy size (number of words covered) of
		 * the new value.
		 */
		budsz = BUDSIZE(newval, tp->dmt_budmin);

		/* try to join.
		 */
		while (budsz < le32_to_cpu(tp->dmt_nleafs)) {
			/* get the buddy leaf.
			 */
			buddy = leafno ^ budsz;

			/* if the leaf's new value is greater than its
			 * buddy's value, we join no more.
			 */
			if (newval > leaf[buddy])
				break;

			/* It shouldn't be less */
			if (newval < leaf[buddy])
				return -EIO;

			/* check which (leafno or buddy) is the left buddy.
			 * the left buddy gets to claim the blocks resulting
			 * from the join while the right gets to claim none.
			 * the left buddy is also eligible to participate in
			 * a join at the next higher level while the right
			 * is not.
			 *
			 */
			if (leafno < buddy) {
				/* leafno is the left buddy.
				 */
				dbAdjTree(tp, buddy, NOFREE);
			} else {
				/* buddy is the left buddy and becomes
				 * leafno.
				 */
				dbAdjTree(tp, leafno, NOFREE);
				leafno = buddy;
			}

			/* on to try the next join.
			 */
			newval += 1;
			budsz <<= 1;
		}
	}

	/* update the leaf value.
	 */
	dbAdjTree(tp, leafno, newval);

	return 0;
}


/*
 * NAME:	dbAdjTree()
 *
 * FUNCTION:	update a leaf of a dmtree with a new value, adjusting
 *		the dmtree, as required, to reflect the new leaf value.
 *		the combination of any buddies must already be done before
 *		this is called.
 *
 * PARAMETERS:
 *	tp	- pointer to the tree to be adjusted.
 *	leafno	- the number of the leaf to be updated.
 *	newval	- the new value for the leaf.
 *
 * RETURN VALUES: none
 */
static void dbAdjTree(dmtree_t * tp, int leafno, int newval)
{
	int lp, pp, k;
	int max;

	/* pick up the index of the leaf for this leafno.
	 */
	lp = leafno + le32_to_cpu(tp->dmt_leafidx);

	/* is the current value the same as the old value ?  if so,
	 * there is nothing to do.
	 */
	if (tp->dmt_stree[lp] == newval)
		return;

	/* set the new value.
	 */
	tp->dmt_stree[lp] = newval;

	/* bubble the new value up the tree as required.
	 */
	for (k = 0; k < le32_to_cpu(tp->dmt_height); k++) {
		/* get the index of the first leaf of the 4 leaf
		 * group containing the specified leaf (leafno).
		 */
		lp = ((lp - 1) & ~0x03) + 1;

		/* get the index of the parent of this 4 leaf group.
		 */
		pp = (lp - 1) >> 2;

		/* determine the maximum of the 4 leaves.
		 */
		max = TREEMAX(&tp->dmt_stree[lp]);

		/* if the maximum of the 4 is the same as the
		 * parent's value, we're done.
		 */
		if (tp->dmt_stree[pp] == max)
			break;

		/* parent gets new value.
		 */
		tp->dmt_stree[pp] = max;

		/* parent becomes leaf for next go-round.
		 */
		lp = pp;
	}
}


/*
 * NAME:	dbFindLeaf()
 *
 * FUNCTION:	search a dmtree_t for sufficient free blocks, returning
 *		the index of a leaf describing the free blocks if
 *		sufficient free blocks are found.
 *
 *		the search starts at the top of the dmtree_t tree and
 *		proceeds down the tree to the leftmost leaf with sufficient
 *		free space.
 *
 * PARAMETERS:
 *	tp	- pointer to the tree to be searched.
 *	l2nb	- log2 number of free blocks to search for.
 *	leafidx	- return pointer to be set to the index of the leaf
 *		  describing at least l2nb free blocks if sufficient
 *		  free blocks are found.
 *
 * RETURN VALUES:
 *	0	- success
 *	-ENOSPC	- insufficient free blocks.
 */
static int dbFindLeaf(dmtree_t * tp, int l2nb, int *leafidx)
{
	int ti, n = 0, k, x = 0;

	/* first check the root of the tree to see if there is
	 * sufficient free space.
	 */
	if (l2nb > tp->dmt_stree[ROOT])
		return -ENOSPC;

	/* sufficient free space available. now search down the tree
	 * starting at the next level for the leftmost leaf that
	 * describes sufficient free space.
	 */
	for (k = le32_to_cpu(tp->dmt_height), ti = 1;
	     k > 0; k--, ti = ((ti + n) << 2) + 1) {
		/* search the four nodes at this level, starting from
		 * the left.
		 */
		for (x = ti, n = 0; n < 4; n++) {
			/* sufficient free space found.  move to the next
			 * level (or quit if this is the last level).
			 */
			if (l2nb <= tp->dmt_stree[x + n])
				break;
		}

		/* better have found something since the higher
		 * levels of the tree said it was here.
		 */
		assert(n < 4);
	}

	/* set the return to the leftmost leaf describing sufficient
	 * free space.
	 */
	*leafidx = x + n - le32_to_cpu(tp->dmt_leafidx);

	return (0);
}


/*
 * NAME:	dbFindBits()
 *
 * FUNCTION:	find a specified number of binary buddy free bits within a
 *		dmap bitmap word value.
 *
 *		this routine searches the bitmap value for (1 << l2nb) free
 *		bits at (1 << l2nb) alignments within the value.
 *
 * PARAMETERS:
 *	word	-  dmap bitmap word value.
 *	l2nb	-  number of free bits specified as a log2 number.
 *
 * RETURN VALUES:
 *	starting bit number of free bits.
 */
static int dbFindBits(u32 word, int l2nb)
{
	int bitno, nb;
	u32 mask;

	/* get the number of bits.
	 */
	nb = 1 << l2nb;
	assert(nb <= DBWORD);

	/* complement the word so we can use a mask (i.e. 0s represent
	 * free bits) and compute the mask.
	 */
	word = ~word;
	mask = ONES << (DBWORD - nb);

	/* scan the word for nb free bits at nb alignments.
	 */
	for (bitno = 0; mask != 0; bitno += nb, mask >>= nb) {
		if ((mask & word) == mask)
			break;
	}

	ASSERT(bitno < 32);

	/* return the bit number.
	 */
	return (bitno);
}


/*
 * NAME:	dbMaxBud(u8 *cp)
 *
 * FUNCTION:	determine the largest binary buddy string of free
 *		bits within 32-bits of the map.
 *
 * PARAMETERS:
 *	cp	-  pointer to the 32-bit value.
 *
 * RETURN VALUES:
 *	largest binary buddy of free bits within a dmap word.
 */
static int dbMaxBud(u8 * cp)
{
	signed char tmp1, tmp2;

	/* check if the wmap word is all free. if so, the
	 * free buddy size is BUDMIN.
	 */
	if (*((uint *) cp) == 0)
		return (BUDMIN);

	/* check if the wmap word is half free. if so, the
	 * free buddy size is BUDMIN-1.
	 */
	if (*((u16 *) cp) == 0 || *((u16 *) cp + 1) == 0)
		return (BUDMIN - 1);

	/* not all free or half free. determine the free buddy
	 * size thru table lookup using quarters of the wmap word.
	 */
	tmp1 = max(budtab[cp[2]], budtab[cp[3]]);
	tmp2 = max(budtab[cp[0]], budtab[cp[1]]);
	return (max(tmp1, tmp2));
}


/*
 * NAME:	cnttz(uint word)
 *
 * FUNCTION:	determine the number of trailing zeros within a 32-bit
 *		value.
 *
 * PARAMETERS:
 *	value	-  32-bit value to be examined.
 *
 * RETURN VALUES:
 *	count of trailing zeros
 */
static int cnttz(u32 word)
{
	int n;

	for (n = 0; n < 32; n++, word >>= 1) {
		if (word & 0x01)
			break;
	}

	return (n);
}


/*
 * NAME:	cntlz(u32 value)
 *
 * FUNCTION:	determine the number of leading zeros within a 32-bit
 *		value.
 *
 * PARAMETERS:
 *	value	-  32-bit value to be examined.
 *
 * RETURN VALUES:
 *	count of leading zeros
 */
static int cntlz(u32 value)
{
	int n;

	for (n = 0; n < 32; n++, value <<= 1) {
		if (value & HIGHORDER)
			break;
	}
	return (n);
}


/*
 * NAME:	blkstol2(s64 nb)
 *
 * FUNCTION:	convert a block count to its log2 value. if the block
 *		count is not a l2 multiple, it is rounded up to the next
 *		larger l2 multiple.
 *
 * PARAMETERS:
 *	nb	-  number of blocks
 *
 * RETURN VALUES:
 *	log2 number of blocks
 */
static int blkstol2(s64 nb)
{
	int l2nb;
	s64 mask;		/* meant to be signed */

	mask = (s64) 1 << (64 - 1);

	/* count the leading bits.
	 */
	for (l2nb = 0; l2nb < 64; l2nb++, mask >>= 1) {
		/* leading bit found.
		 */
		if (nb & mask) {
			/* determine the l2 value.
			 */
			l2nb = (64 - 1) - l2nb;

			/* check if we need to round up.
			 */
			if (~mask & nb)
				l2nb++;

			return (l2nb);
		}
	}
	assert(0);
	return 0;		/* fix compiler warning */
}


/*
 * NAME:	dbAllocBottomUp()
 *
 * FUNCTION:	alloc the specified block range from the working block
 *		allocation map.
 *
 *		the blocks will be alloc from the working map one dmap
 *		at a time.
 *
 * PARAMETERS:
 *	ip	-  pointer to in-core inode;
 *	blkno	-  starting block number to be freed.
 *	nblocks	-  number of blocks to be freed.
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error
 */
int dbAllocBottomUp(struct inode *ip, s64 blkno, s64 nblocks)
{
	struct metapage *mp;
	struct dmap *dp;
	int nb, rc;
	s64 lblkno, rem;
	struct inode *ipbmap = JFS_SBI(ip->i_sb)->ipbmap;
	struct bmap *bmp = JFS_SBI(ip->i_sb)->bmap;

	IREAD_LOCK(ipbmap, RDWRLOCK_DMAP);

	/* block to be allocated better be within the mapsize. */
	ASSERT(nblocks <= bmp->db_mapsize - blkno);

	/*
	 * allocate the blocks a dmap at a time.
	 */
	mp = NULL;
	for (rem = nblocks; rem > 0; rem -= nb, blkno += nb) {
		/* release previous dmap if any */
		if (mp) {
			write_metapage(mp);
		}

		/* get the buffer for the current dmap. */
		lblkno = BLKTODMAP(blkno, bmp->db_l2nbperpage);
		mp = read_metapage(ipbmap, lblkno, PSIZE, 0);
		if (mp == NULL) {
			IREAD_UNLOCK(ipbmap);
			return -EIO;
		}
		dp = (struct dmap *) mp->data;

		/* determine the number of blocks to be allocated from
		 * this dmap.
		 */
		nb = min(rem, BPERDMAP - (blkno & (BPERDMAP - 1)));

		/* allocate the blocks. */
		if ((rc = dbAllocDmapBU(bmp, dp, blkno, nb))) {
			release_metapage(mp);
			IREAD_UNLOCK(ipbmap);
			return (rc);
		}
	}

	/* write the last buffer. */
	write_metapage(mp);

	IREAD_UNLOCK(ipbmap);

	return (0);
}


static int dbAllocDmapBU(struct bmap * bmp, struct dmap * dp, s64 blkno,
			 int nblocks)
{
	int rc;
	int dbitno, word, rembits, nb, nwords, wbitno, agno;
	s8 oldroot;
	struct dmaptree *tp = (struct dmaptree *) & dp->tree;

	/* save the current value of the root (i.e. maximum free string)
	 * of the dmap tree.
	 */
	oldroot = tp->stree[ROOT];

	/* determine the bit number and word within the dmap of the
	 * starting block.
	 */
	dbitno = blkno & (BPERDMAP - 1);
	word = dbitno >> L2DBWORD;

	/* block range better be within the dmap */
	assert(dbitno + nblocks <= BPERDMAP);

	/* allocate the bits of the dmap's words corresponding to the block
	 * range. not all bits of the first and last words may be contained
	 * within the block range.  if this is the case, we'll work against
	 * those words (i.e. partial first and/or last) on an individual basis
	 * (a single pass), allocating the bits of interest by hand and
	 * updating the leaf corresponding to the dmap word. a single pass
	 * will be used for all dmap words fully contained within the
	 * specified range.  within this pass, the bits of all fully contained
	 * dmap words will be marked as free in a single shot and the leaves
	 * will be updated. a single leaf may describe the free space of
	 * multiple dmap words, so we may update only a subset of the actual
	 * leaves corresponding to the dmap words of the block range.
	 */
	for (rembits = nblocks; rembits > 0; rembits -= nb, dbitno += nb) {
		/* determine the bit number within the word and
		 * the number of bits within the word.
		 */
		wbitno = dbitno & (DBWORD - 1);
		nb = min(rembits, DBWORD - wbitno);

		/* check if only part of a word is to be allocated.
		 */
		if (nb < DBWORD) {
			/* allocate (set to 1) the appropriate bits within
			 * this dmap word.
			 */
			dp->wmap[word] |= cpu_to_le32(ONES << (DBWORD - nb)
						      >> wbitno);

			word++;
		} else {
			/* one or more dmap words are fully contained
			 * within the block range.  determine how many
			 * words and allocate (set to 1) the bits of these
			 * words.
			 */
			nwords = rembits >> L2DBWORD;
			memset(&dp->wmap[word], (int) ONES, nwords * 4);

			/* determine how many bits */
			nb = nwords << L2DBWORD;
			word += nwords;
		}
	}

	/* update the free count for this dmap */
	le32_add_cpu(&dp->nfree, -nblocks);

	/* reconstruct summary tree */
	dbInitDmapTree(dp);

	BMAP_LOCK(bmp);

	/* if this allocation group is completely free,
	 * update the highest active allocation group number
	 * if this allocation group is the new max.
	 */
	agno = blkno >> bmp->db_agl2size;
	if (agno > bmp->db_maxag)
		bmp->db_maxag = agno;

	/* update the free count for the allocation group and map */
	bmp->db_agfree[agno] -= nblocks;
	bmp->db_nfree -= nblocks;

	BMAP_UNLOCK(bmp);

	/* if the root has not changed, done. */
	if (tp->stree[ROOT] == oldroot)
		return (0);

	/* root changed. bubble the change up to the dmap control pages.
	 * if the adjustment of the upper level control pages fails,
	 * backout the bit allocation (thus making everything consistent).
	 */
	if ((rc = dbAdjCtl(bmp, blkno, tp->stree[ROOT], 1, 0)))
		dbFreeBits(bmp, dp, blkno, nblocks);

	return (rc);
}


/*
 * NAME:	dbExtendFS()
 *
 * FUNCTION:	extend bmap from blkno for nblocks;
 *		dbExtendFS() updates bmap ready for dbAllocBottomUp();
 *
 * L2
 *  |
 *   L1---------------------------------L1
 *    |					 |
 *     L0---------L0---------L0		  L0---------L0---------L0
 *      |	   |	      |		   |	      |		 |
 *	 d0,...,dn  d0,...,dn  d0,...,dn    d0,...,dn  d0,...,dn  d0,.,dm;
 * L2L1L0d0,...,dnL0d0,...,dnL0d0,...,dnL1L0d0,...,dnL0d0,...,dnL0d0,..dm
 *
 * <---old---><----------------------------extend----------------------->
 */
int dbExtendFS(struct inode *ipbmap, s64 blkno,	s64 nblocks)
{
	struct jfs_sb_info *sbi = JFS_SBI(ipbmap->i_sb);
	int nbperpage = sbi->nbperpage;
	int i, i0 = true, j, j0 = true, k, n;
	s64 newsize;
	s64 p;
	struct metapage *mp, *l2mp, *l1mp = NULL, *l0mp = NULL;
	struct dmapctl *l2dcp, *l1dcp, *l0dcp;
	struct dmap *dp;
	s8 *l0leaf, *l1leaf, *l2leaf;
	struct bmap *bmp = sbi->bmap;
	int agno, l2agsize, oldl2agsize;
	s64 ag_rem;

	newsize = blkno + nblocks;

	jfs_info("dbExtendFS: blkno:%Ld nblocks:%Ld newsize:%Ld",
		 (long long) blkno, (long long) nblocks, (long long) newsize);

	/*
	 *	initialize bmap control page.
	 *
	 * all the data in bmap control page should exclude
	 * the mkfs hidden dmap page.
	 */

	/* update mapsize */
	bmp->db_mapsize = newsize;
	bmp->db_maxlevel = BMAPSZTOLEV(bmp->db_mapsize);

	/* compute new AG size */
	l2agsize = dbGetL2AGSize(newsize);
	oldl2agsize = bmp->db_agl2size;

	bmp->db_agl2size = l2agsize;
	bmp->db_agsize = 1 << l2agsize;

	/* compute new number of AG */
	agno = bmp->db_numag;
	bmp->db_numag = newsize >> l2agsize;
	bmp->db_numag += ((u32) newsize % (u32) bmp->db_agsize) ? 1 : 0;

	/*
	 *	reconfigure db_agfree[]
	 * from old AG configuration to new AG configuration;
	 *
	 * coalesce contiguous k (newAGSize/oldAGSize) AGs;
	 * i.e., (AGi, ..., AGj) where i = k*n and j = k*(n+1) - 1 to AGn;
	 * note: new AG size = old AG size * (2**x).
	 */
	if (l2agsize == oldl2agsize)
		goto extend;
	k = 1 << (l2agsize - oldl2agsize);
	ag_rem = bmp->db_agfree[0];	/* save agfree[0] */
	for (i = 0, n = 0; i < agno; n++) {
		bmp->db_agfree[n] = 0;	/* init collection point */

		/* coalesce contiguous k AGs; */
		for (j = 0; j < k && i < agno; j++, i++) {
			/* merge AGi to AGn */
			bmp->db_agfree[n] += bmp->db_agfree[i];
		}
	}
	bmp->db_agfree[0] += ag_rem;	/* restore agfree[0] */

	for (; n < MAXAG; n++)
		bmp->db_agfree[n] = 0;

	/*
	 * update highest active ag number
	 */

	bmp->db_maxag = bmp->db_maxag / k;

	/*
	 *	extend bmap
	 *
	 * update bit maps and corresponding level control pages;
	 * global control page db_nfree, db_agfree[agno], db_maxfreebud;
	 */
      extend:
	/* get L2 page */
	p = BMAPBLKNO + nbperpage;	/* L2 page */
	l2mp = read_metapage(ipbmap, p, PSIZE, 0);
	if (!l2mp) {
		jfs_error(ipbmap->i_sb, "L2 page could not be read\n");
		return -EIO;
	}
	l2dcp = (struct dmapctl *) l2mp->data;

	/* compute start L1 */
	k = blkno >> L2MAXL1SIZE;
	l2leaf = l2dcp->stree + CTLLEAFIND + k;
	p = BLKTOL1(blkno, sbi->l2nbperpage);	/* L1 page */

	/*
	 * extend each L1 in L2
	 */
	for (; k < LPERCTL; k++, p += nbperpage) {
		/* get L1 page */
		if (j0) {
			/* read in L1 page: (blkno & (MAXL1SIZE - 1)) */
			l1mp = read_metapage(ipbmap, p, PSIZE, 0);
			if (l1mp == NULL)
				goto errout;
			l1dcp = (struct dmapctl *) l1mp->data;

			/* compute start L0 */
			j = (blkno & (MAXL1SIZE - 1)) >> L2MAXL0SIZE;
			l1leaf = l1dcp->stree + CTLLEAFIND + j;
			p = BLKTOL0(blkno, sbi->l2nbperpage);
			j0 = false;
		} else {
			/* assign/init L1 page */
			l1mp = get_metapage(ipbmap, p, PSIZE, 0);
			if (l1mp == NULL)
				goto errout;

			l1dcp = (struct dmapctl *) l1mp->data;

			/* compute start L0 */
			j = 0;
			l1leaf = l1dcp->stree + CTLLEAFIND;
			p += nbperpage;	/* 1st L0 of L1.k */
		}

		/*
		 * extend each L0 in L1
		 */
		for (; j < LPERCTL; j++) {
			/* get L0 page */
			if (i0) {
				/* read in L0 page: (blkno & (MAXL0SIZE - 1)) */

				l0mp = read_metapage(ipbmap, p, PSIZE, 0);
				if (l0mp == NULL)
					goto errout;
				l0dcp = (struct dmapctl *) l0mp->data;

				/* compute start dmap */
				i = (blkno & (MAXL0SIZE - 1)) >>
				    L2BPERDMAP;
				l0leaf = l0dcp->stree + CTLLEAFIND + i;
				p = BLKTODMAP(blkno,
					      sbi->l2nbperpage);
				i0 = false;
			} else {
				/* assign/init L0 page */
				l0mp = get_metapage(ipbmap, p, PSIZE, 0);
				if (l0mp == NULL)
					goto errout;

				l0dcp = (struct dmapctl *) l0mp->data;

				/* compute start dmap */
				i = 0;
				l0leaf = l0dcp->stree + CTLLEAFIND;
				p += nbperpage;	/* 1st dmap of L0.j */
			}

			/*
			 * extend each dmap in L0
			 */
			for (; i < LPERCTL; i++) {
				/*
				 * reconstruct the dmap page, and
				 * initialize corresponding parent L0 leaf
				 */
				if ((n = blkno & (BPERDMAP - 1))) {
					/* read in dmap page: */
					mp = read_metapage(ipbmap, p,
							   PSIZE, 0);
					if (mp == NULL)
						goto errout;
					n = min(nblocks, (s64)BPERDMAP - n);
				} else {
					/* assign/init dmap page */
					mp = read_metapage(ipbmap, p,
							   PSIZE, 0);
					if (mp == NULL)
						goto errout;

					n = min_t(s64, nblocks, BPERDMAP);
				}

				dp = (struct dmap *) mp->data;
				*l0leaf = dbInitDmap(dp, blkno, n);

				bmp->db_nfree += n;
				agno = le64_to_cpu(dp->start) >> l2agsize;
				bmp->db_agfree[agno] += n;

				write_metapage(mp);

				l0leaf++;
				p += nbperpage;

				blkno += n;
				nblocks -= n;
				if (nblocks == 0)
					break;
			}	/* for each dmap in a L0 */

			/*
			 * build current L0 page from its leaves, and
			 * initialize corresponding parent L1 leaf
			 */
			*l1leaf = dbInitDmapCtl(l0dcp, 0, ++i);
			write_metapage(l0mp);
			l0mp = NULL;

			if (nblocks)
				l1leaf++;	/* continue for next L0 */
			else {
				/* more than 1 L0 ? */
				if (j > 0)
					break;	/* build L1 page */
				else {
					/* summarize in global bmap page */
					bmp->db_maxfreebud = *l1leaf;
					release_metapage(l1mp);
					release_metapage(l2mp);
					goto finalize;
				}
			}
		}		/* for each L0 in a L1 */

		/*
		 * build current L1 page from its leaves, and
		 * initialize corresponding parent L2 leaf
		 */
		*l2leaf = dbInitDmapCtl(l1dcp, 1, ++j);
		write_metapage(l1mp);
		l1mp = NULL;

		if (nblocks)
			l2leaf++;	/* continue for next L1 */
		else {
			/* more than 1 L1 ? */
			if (k > 0)
				break;	/* build L2 page */
			else {
				/* summarize in global bmap page */
				bmp->db_maxfreebud = *l2leaf;
				release_metapage(l2mp);
				goto finalize;
			}
		}
	}			/* for each L1 in a L2 */

	jfs_error(ipbmap->i_sb, "function has not returned as expected\n");
errout:
	if (l0mp)
		release_metapage(l0mp);
	if (l1mp)
		release_metapage(l1mp);
	release_metapage(l2mp);
	return -EIO;

	/*
	 *	finalize bmap control page
	 */
finalize:

	return 0;
}


/*
 *	dbFinalizeBmap()
 */
void dbFinalizeBmap(struct inode *ipbmap)
{
	struct bmap *bmp = JFS_SBI(ipbmap->i_sb)->bmap;
	int actags, inactags, l2nl;
	s64 ag_rem, actfree, inactfree, avgfree;
	int i, n;

	/*
	 *	finalize bmap control page
	 */
//finalize:
	/*
	 * compute db_agpref: preferred ag to allocate from
	 * (the leftmost ag with average free space in it);
	 */
//agpref:
	/* get the number of active ags and inactive ags */
	actags = bmp->db_maxag + 1;
	inactags = bmp->db_numag - actags;
	ag_rem = bmp->db_mapsize & (bmp->db_agsize - 1);	/* ??? */

	/* determine how many blocks are in the inactive allocation
	 * groups. in doing this, we must account for the fact that
	 * the rightmost group might be a partial group (i.e. file
	 * system size is not a multiple of the group size).
	 */
	inactfree = (inactags && ag_rem) ?
	    ((inactags - 1) << bmp->db_agl2size) + ag_rem
	    : inactags << bmp->db_agl2size;

	/* determine how many free blocks are in the active
	 * allocation groups plus the average number of free blocks
	 * within the active ags.
	 */
	actfree = bmp->db_nfree - inactfree;
	avgfree = (u32) actfree / (u32) actags;

	/* if the preferred allocation group has not average free space.
	 * re-establish the preferred group as the leftmost
	 * group with average free space.
	 */
	if (bmp->db_agfree[bmp->db_agpref] < avgfree) {
		for (bmp->db_agpref = 0; bmp->db_agpref < actags;
		     bmp->db_agpref++) {
			if (bmp->db_agfree[bmp->db_agpref] >= avgfree)
				break;
		}
		if (bmp->db_agpref >= bmp->db_numag) {
			jfs_error(ipbmap->i_sb,
				  "cannot find ag with average freespace\n");
		}
	}

	/*
	 * compute db_aglevel, db_agheight, db_width, db_agstart:
	 * an ag is covered in aglevel dmapctl summary tree,
	 * at agheight level height (from leaf) with agwidth number of nodes
	 * each, which starts at agstart index node of the smmary tree node
	 * array;
	 */
	bmp->db_aglevel = BMAPSZTOLEV(bmp->db_agsize);
	l2nl =
	    bmp->db_agl2size - (L2BPERDMAP + bmp->db_aglevel * L2LPERCTL);
	bmp->db_agheight = l2nl >> 1;
	bmp->db_agwidth = 1 << (l2nl - (bmp->db_agheight << 1));
	for (i = 5 - bmp->db_agheight, bmp->db_agstart = 0, n = 1; i > 0;
	     i--) {
		bmp->db_agstart += n;
		n <<= 2;
	}

}


/*
 * NAME:	dbInitDmap()/ujfs_idmap_page()
 *
 * FUNCTION:	initialize working/persistent bitmap of the dmap page
 *		for the specified number of blocks:
 *
 *		at entry, the bitmaps had been initialized as free (ZEROS);
 *		The number of blocks will only account for the actually
 *		existing blocks. Blocks which don't actually exist in
 *		the aggregate will be marked as allocated (ONES);
 *
 * PARAMETERS:
 *	dp	- pointer to page of map
 *	nblocks	- number of blocks this page
 *
 * RETURNS: NONE
 */
static int dbInitDmap(struct dmap * dp, s64 Blkno, int nblocks)
{
	int blkno, w, b, r, nw, nb, i;

	/* starting block number within the dmap */
	blkno = Blkno & (BPERDMAP - 1);

	if (blkno == 0) {
		dp->nblocks = dp->nfree = cpu_to_le32(nblocks);
		dp->start = cpu_to_le64(Blkno);

		if (nblocks == BPERDMAP) {
			memset(&dp->wmap[0], 0, LPERDMAP * 4);
			memset(&dp->pmap[0], 0, LPERDMAP * 4);
			goto initTree;
		}
	} else {
		le32_add_cpu(&dp->nblocks, nblocks);
		le32_add_cpu(&dp->nfree, nblocks);
	}

	/* word number containing start block number */
	w = blkno >> L2DBWORD;

	/*
	 * free the bits corresponding to the block range (ZEROS):
	 * note: not all bits of the first and last words may be contained
	 * within the block range.
	 */
	for (r = nblocks; r > 0; r -= nb, blkno += nb) {
		/* number of bits preceding range to be freed in the word */
		b = blkno & (DBWORD - 1);
		/* number of bits to free in the word */
		nb = min(r, DBWORD - b);

		/* is partial word to be freed ? */
		if (nb < DBWORD) {
			/* free (set to 0) from the bitmap word */
			dp->wmap[w] &= cpu_to_le32(~(ONES << (DBWORD - nb)
						     >> b));
			dp->pmap[w] &= cpu_to_le32(~(ONES << (DBWORD - nb)
						     >> b));

			/* skip the word freed */
			w++;
		} else {
			/* free (set to 0) contiguous bitmap words */
			nw = r >> L2DBWORD;
			memset(&dp->wmap[w], 0, nw * 4);
			memset(&dp->pmap[w], 0, nw * 4);

			/* skip the words freed */
			nb = nw << L2DBWORD;
			w += nw;
		}
	}

	/*
	 * mark bits following the range to be freed (non-existing
	 * blocks) as allocated (ONES)
	 */

	if (blkno == BPERDMAP)
		goto initTree;

	/* the first word beyond the end of existing blocks */
	w = blkno >> L2DBWORD;

	/* does nblocks fall on a 32-bit boundary ? */
	b = blkno & (DBWORD - 1);
	if (b) {
		/* mark a partial word allocated */
		dp->wmap[w] = dp->pmap[w] = cpu_to_le32(ONES >> b);
		w++;
	}

	/* set the rest of the words in the page to allocated (ONES) */
	for (i = w; i < LPERDMAP; i++)
		dp->pmap[i] = dp->wmap[i] = cpu_to_le32(ONES);

	/*
	 * init tree
	 */
      initTree:
	return (dbInitDmapTree(dp));
}


/*
 * NAME:	dbInitDmapTree()/ujfs_complete_dmap()
 *
 * FUNCTION:	initialize summary tree of the specified dmap:
 *
 *		at entry, bitmap of the dmap has been initialized;
 *
 * PARAMETERS:
 *	dp	- dmap to complete
 *	blkno	- starting block number for this dmap
 *	treemax	- will be filled in with max free for this dmap
 *
 * RETURNS:	max free string at the root of the tree
 */
static int dbInitDmapTree(struct dmap * dp)
{
	struct dmaptree *tp;
	s8 *cp;
	int i;

	/* init fixed info of tree */
	tp = &dp->tree;
	tp->nleafs = cpu_to_le32(LPERDMAP);
	tp->l2nleafs = cpu_to_le32(L2LPERDMAP);
	tp->leafidx = cpu_to_le32(LEAFIND);
	tp->height = cpu_to_le32(4);
	tp->budmin = BUDMIN;

	/* init each leaf from corresponding wmap word:
	 * note: leaf is set to NOFREE(-1) if all blocks of corresponding
	 * bitmap word are allocated.
	 */
	cp = tp->stree + le32_to_cpu(tp->leafidx);
	for (i = 0; i < LPERDMAP; i++)
		*cp++ = dbMaxBud((u8 *) & dp->wmap[i]);

	/* build the dmap's binary buddy summary tree */
	return (dbInitTree(tp));
}


/*
 * NAME:	dbInitTree()/ujfs_adjtree()
 *
 * FUNCTION:	initialize binary buddy summary tree of a dmap or dmapctl.
 *
 *		at entry, the leaves of the tree has been initialized
 *		from corresponding bitmap word or root of summary tree
 *		of the child control page;
 *		configure binary buddy system at the leaf level, then
 *		bubble up the values of the leaf nodes up the tree.
 *
 * PARAMETERS:
 *	cp	- Pointer to the root of the tree
 *	l2leaves- Number of leaf nodes as a power of 2
 *	l2min	- Number of blocks that can be covered by a leaf
 *		  as a power of 2
 *
 * RETURNS: max free string at the root of the tree
 */
static int dbInitTree(struct dmaptree * dtp)
{
	int l2max, l2free, bsize, nextb, i;
	int child, parent, nparent;
	s8 *tp, *cp, *cp1;

	tp = dtp->stree;

	/* Determine the maximum free string possible for the leaves */
	l2max = le32_to_cpu(dtp->l2nleafs) + dtp->budmin;

	/*
	 * configure the leaf levevl into binary buddy system
	 *
	 * Try to combine buddies starting with a buddy size of 1
	 * (i.e. two leaves). At a buddy size of 1 two buddy leaves
	 * can be combined if both buddies have a maximum free of l2min;
	 * the combination will result in the left-most buddy leaf having
	 * a maximum free of l2min+1.
	 * After processing all buddies for a given size, process buddies
	 * at the next higher buddy size (i.e. current size * 2) and
	 * the next maximum free (current free + 1).
	 * This continues until the maximum possible buddy combination
	 * yields maximum free.
	 */
	for (l2free = dtp->budmin, bsize = 1; l2free < l2max;
	     l2free++, bsize = nextb) {
		/* get next buddy size == current buddy pair size */
		nextb = bsize << 1;

		/* scan each adjacent buddy pair at current buddy size */
		for (i = 0, cp = tp + le32_to_cpu(dtp->leafidx);
		     i < le32_to_cpu(dtp->nleafs);
		     i += nextb, cp += nextb) {
			/* coalesce if both adjacent buddies are max free */
			if (*cp == l2free && *(cp + bsize) == l2free) {
				*cp = l2free + 1;	/* left take right */
				*(cp + bsize) = -1;	/* right give left */
			}
		}
	}

	/*
	 * bubble summary information of leaves up the tree.
	 *
	 * Starting at the leaf node level, the four nodes described by
	 * the higher level parent node are compared for a maximum free and
	 * this maximum becomes the value of the parent node.
	 * when all lower level nodes are processed in this fashion then
	 * move up to the next level (parent becomes a lower level node) and
	 * continue the process for that level.
	 */
	for (child = le32_to_cpu(dtp->leafidx),
	     nparent = le32_to_cpu(dtp->nleafs) >> 2;
	     nparent > 0; nparent >>= 2, child = parent) {
		/* get index of 1st node of parent level */
		parent = (child - 1) >> 2;

		/* set the value of the parent node as the maximum
		 * of the four nodes of the current level.
		 */
		for (i = 0, cp = tp + child, cp1 = tp + parent;
		     i < nparent; i++, cp += 4, cp1++)
			*cp1 = TREEMAX(cp);
	}

	return (*tp);
}


/*
 *	dbInitDmapCtl()
 *
 * function: initialize dmapctl page
 */
static int dbInitDmapCtl(struct dmapctl * dcp, int level, int i)
{				/* start leaf index not covered by range */
	s8 *cp;

	dcp->nleafs = cpu_to_le32(LPERCTL);
	dcp->l2nleafs = cpu_to_le32(L2LPERCTL);
	dcp->leafidx = cpu_to_le32(CTLLEAFIND);
	dcp->height = cpu_to_le32(5);
	dcp->budmin = L2BPERDMAP + L2LPERCTL * level;

	/*
	 * initialize the leaves of current level that were not covered
	 * by the specified input block range (i.e. the leaves have no
	 * low level dmapctl or dmap).
	 */
	cp = &dcp->stree[CTLLEAFIND + i];
	for (; i < LPERCTL; i++)
		*cp++ = NOFREE;

	/* build the dmap's binary buddy summary tree */
	return (dbInitTree((struct dmaptree *) dcp));
}


/*
 * NAME:	dbGetL2AGSize()/ujfs_getagl2size()
 *
 * FUNCTION:	Determine log2(allocation group size) from aggregate size
 *
 * PARAMETERS:
 *	nblocks	- Number of blocks in aggregate
 *
 * RETURNS: log2(allocation group size) in aggregate blocks
 */
static int dbGetL2AGSize(s64 nblocks)
{
	s64 sz;
	s64 m;
	int l2sz;

	if (nblocks < BPERDMAP * MAXAG)
		return (L2BPERDMAP);

	/* round up aggregate size to power of 2 */
	m = ((u64) 1 << (64 - 1));
	for (l2sz = 64; l2sz >= 0; l2sz--, m >>= 1) {
		if (m & nblocks)
			break;
	}

	sz = (s64) 1 << l2sz;
	if (sz < nblocks)
		l2sz += 1;

	/* agsize = roundupSize/max_number_of_ag */
	return (l2sz - L2MAXAG);
}


/*
 * NAME:	dbMapFileSizeToMapSize()
 *
 * FUNCTION:	compute number of blocks the block allocation map file
 *		can cover from the map file size;
 *
 * RETURNS:	Number of blocks which can be covered by this block map file;
 */

/*
 * maximum number of map pages at each level including control pages
 */
#define MAXL0PAGES	(1 + LPERCTL)
#define MAXL1PAGES	(1 + LPERCTL * MAXL0PAGES)

/*
 * convert number of map pages to the zero origin top dmapctl level
 */
#define BMAPPGTOLEV(npages)	\
	(((npages) <= 3 + MAXL0PAGES) ? 0 : \
	 ((npages) <= 2 + MAXL1PAGES) ? 1 : 2)

s64 dbMapFileSizeToMapSize(struct inode * ipbmap)
{
	struct super_block *sb = ipbmap->i_sb;
	s64 nblocks;
	s64 npages, ndmaps;
	int level, i;
	int complete, factor;

	nblocks = ipbmap->i_size >> JFS_SBI(sb)->l2bsize;
	npages = nblocks >> JFS_SBI(sb)->l2nbperpage;
	level = BMAPPGTOLEV(npages);

	/* At each level, accumulate the number of dmap pages covered by
	 * the number of full child levels below it;
	 * repeat for the last incomplete child level.
	 */
	ndmaps = 0;
	npages--;		/* skip the first global control page */
	/* skip higher level control pages above top level covered by map */
	npages -= (2 - level);
	npages--;		/* skip top level's control page */
	for (i = level; i >= 0; i--) {
		factor =
		    (i == 2) ? MAXL1PAGES : ((i == 1) ? MAXL0PAGES : 1);
		complete = (u32) npages / factor;
		ndmaps += complete * ((i == 2) ? LPERCTL * LPERCTL :
				      ((i == 1) ? LPERCTL : 1));

		/* pages in last/incomplete child */
		npages = (u32) npages % factor;
		/* skip incomplete child's level control page */
		npages--;
	}

	/* convert the number of dmaps into the number of blocks
	 * which can be covered by the dmaps;
	 */
	nblocks = ndmaps << L2BPERDMAP;

	return (nblocks);
}
