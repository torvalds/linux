// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines  Corp., 2000-2004
*/

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/quotaops.h>
#include <linux/blkdev.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_metapage.h"
#include "jfs_dinode.h"
#include "jfs_imap.h"
#include "jfs_dmap.h"
#include "jfs_superblock.h"
#include "jfs_txnmgr.h"
#include "jfs_debug.h"

#define BITSPERPAGE	(PSIZE << 3)
#define L2MEGABYTE	20
#define MEGABYTE	(1 << L2MEGABYTE)
#define MEGABYTE32	(MEGABYTE << 5)

/* convert block number to bmap file page number */
#define BLKTODMAPN(b)\
	(((b) >> 13) + ((b) >> 23) + ((b) >> 33) + 3 + 1)

/*
 *	jfs_extendfs()
 *
 * function: extend file system;
 *
 *   |-------------------------------|----------|----------|
 *   file system space               fsck       inline log
 *                                   workspace  space
 *
 * input:
 *	new LVSize: in LV blocks (required)
 *	new LogSize: in LV blocks (optional)
 *	new FSSize: in LV blocks (optional)
 *
 * new configuration:
 * 1. set new LogSize as specified or default from new LVSize;
 * 2. compute new FSCKSize from new LVSize;
 * 3. set new FSSize as MIN(FSSize, LVSize-(LogSize+FSCKSize)) where
 *    assert(new FSSize >= old FSSize),
 *    i.e., file system must not be shrunk;
 */
int jfs_extendfs(struct super_block *sb, s64 newLVSize, int newLogSize)
{
	int rc = 0;
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct inode *ipbmap = sbi->ipbmap;
	struct inode *ipbmap2;
	struct inode *ipimap = sbi->ipimap;
	struct jfs_log *log = sbi->log;
	struct bmap *bmp = sbi->bmap;
	s64 newLogAddress, newFSCKAddress;
	int newFSCKSize;
	s64 newMapSize = 0, mapSize;
	s64 XAddress, XSize, nblocks, xoff, xaddr, t64;
	s64 oldLVSize;
	s64 newFSSize;
	s64 VolumeSize;
	int newNpages = 0, nPages, newPage, xlen, t32;
	int tid;
	int log_formatted = 0;
	struct inode *iplist[1];
	struct jfs_superblock *j_sb, *j_sb2;
	s64 old_agsize;
	int agsizechanged = 0;
	struct buffer_head *bh, *bh2;

	/* If the volume hasn't grown, get out now */

	if (sbi->mntflag & JFS_INLINELOG)
		oldLVSize = addressPXD(&sbi->logpxd) + lengthPXD(&sbi->logpxd);
	else
		oldLVSize = addressPXD(&sbi->fsckpxd) +
		    lengthPXD(&sbi->fsckpxd);

	if (oldLVSize >= newLVSize) {
		printk(KERN_WARNING
		       "jfs_extendfs: volume hasn't grown, returning\n");
		goto out;
	}

	VolumeSize = i_size_read(sb->s_bdev->bd_inode) >> sb->s_blocksize_bits;

	if (VolumeSize) {
		if (newLVSize > VolumeSize) {
			printk(KERN_WARNING "jfs_extendfs: invalid size\n");
			rc = -EINVAL;
			goto out;
		}
	} else {
		/* check the device */
		bh = sb_bread(sb, newLVSize - 1);
		if (!bh) {
			printk(KERN_WARNING "jfs_extendfs: invalid size\n");
			rc = -EINVAL;
			goto out;
		}
		bforget(bh);
	}

	/* Can't extend write-protected drive */

	if (isReadOnly(ipbmap)) {
		printk(KERN_WARNING "jfs_extendfs: read-only file system\n");
		rc = -EROFS;
		goto out;
	}

	/*
	 *	reconfigure LV spaces
	 *	---------------------
	 *
	 * validate new size, or, if not specified, determine new size
	 */

	/*
	 * reconfigure inline log space:
	 */
	if ((sbi->mntflag & JFS_INLINELOG)) {
		if (newLogSize == 0) {
			/*
			 * no size specified: default to 1/256 of aggregate
			 * size; rounded up to a megabyte boundary;
			 */
			newLogSize = newLVSize >> 8;
			t32 = (1 << (20 - sbi->l2bsize)) - 1;
			newLogSize = (newLogSize + t32) & ~t32;
			newLogSize =
			    min(newLogSize, MEGABYTE32 >> sbi->l2bsize);
		} else {
			/*
			 * convert the newLogSize to fs blocks.
			 *
			 * Since this is given in megabytes, it will always be
			 * an even number of pages.
			 */
			newLogSize = (newLogSize * MEGABYTE) >> sbi->l2bsize;
		}

	} else
		newLogSize = 0;

	newLogAddress = newLVSize - newLogSize;

	/*
	 * reconfigure fsck work space:
	 *
	 * configure it to the end of the logical volume regardless of
	 * whether file system extends to the end of the aggregate;
	 * Need enough 4k pages to cover:
	 *  - 1 bit per block in aggregate rounded up to BPERDMAP boundary
	 *  - 1 extra page to handle control page and intermediate level pages
	 *  - 50 extra pages for the chkdsk service log
	 */
	t64 = ((newLVSize - newLogSize + BPERDMAP - 1) >> L2BPERDMAP)
	    << L2BPERDMAP;
	t32 = DIV_ROUND_UP(t64, BITSPERPAGE) + 1 + 50;
	newFSCKSize = t32 << sbi->l2nbperpage;
	newFSCKAddress = newLogAddress - newFSCKSize;

	/*
	 * compute new file system space;
	 */
	newFSSize = newLVSize - newLogSize - newFSCKSize;

	/* file system cannot be shrunk */
	if (newFSSize < bmp->db_mapsize) {
		rc = -EINVAL;
		goto out;
	}

	/*
	 * If we're expanding enough that the inline log does not overlap
	 * the old one, we can format the new log before we quiesce the
	 * filesystem.
	 */
	if ((sbi->mntflag & JFS_INLINELOG) && (newLogAddress > oldLVSize)) {
		if ((rc = lmLogFormat(log, newLogAddress, newLogSize)))
			goto out;
		log_formatted = 1;
	}
	/*
	 *	quiesce file system
	 *
	 * (prepare to move the inline log and to prevent map update)
	 *
	 * block any new transactions and wait for completion of
	 * all wip transactions and flush modified pages s.t.
	 * on-disk file system is in consistent state and
	 * log is not required for recovery.
	 */
	txQuiesce(sb);

	/* Reset size of direct inode */
	sbi->direct_inode->i_size =  i_size_read(sb->s_bdev->bd_inode);

	if (sbi->mntflag & JFS_INLINELOG) {
		/*
		 * deactivate old inline log
		 */
		lmLogShutdown(log);

		/*
		 * mark on-disk super block for fs in transition;
		 *
		 * update on-disk superblock for the new space configuration
		 * of inline log space and fsck work space descriptors:
		 * N.B. FS descriptor is NOT updated;
		 *
		 * crash recovery:
		 * logredo(): if FM_EXTENDFS, return to fsck() for cleanup;
		 * fsck(): if FM_EXTENDFS, reformat inline log and fsck
		 * workspace from superblock inline log descriptor and fsck
		 * workspace descriptor;
		 */

		/* read in superblock */
		if ((rc = readSuper(sb, &bh)))
			goto error_out;
		j_sb = (struct jfs_superblock *)bh->b_data;

		/* mark extendfs() in progress */
		j_sb->s_state |= cpu_to_le32(FM_EXTENDFS);
		j_sb->s_xsize = cpu_to_le64(newFSSize);
		PXDaddress(&j_sb->s_xfsckpxd, newFSCKAddress);
		PXDlength(&j_sb->s_xfsckpxd, newFSCKSize);
		PXDaddress(&j_sb->s_xlogpxd, newLogAddress);
		PXDlength(&j_sb->s_xlogpxd, newLogSize);

		/* synchronously update superblock */
		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
		brelse(bh);

		/*
		 * format new inline log synchronously;
		 *
		 * crash recovery: if log move in progress,
		 * reformat log and exit success;
		 */
		if (!log_formatted)
			if ((rc = lmLogFormat(log, newLogAddress, newLogSize)))
				goto error_out;

		/*
		 * activate new log
		 */
		log->base = newLogAddress;
		log->size = newLogSize >> (L2LOGPSIZE - sb->s_blocksize_bits);
		if ((rc = lmLogInit(log)))
			goto error_out;
	}

	/*
	 *	extend block allocation map
	 *	---------------------------
	 *
	 * extendfs() for new extension, retry after crash recovery;
	 *
	 * note: both logredo() and fsck() rebuild map from
	 * the bitmap and configuration parameter from superblock
	 * (disregarding all other control information in the map);
	 *
	 * superblock:
	 *  s_size: aggregate size in physical blocks;
	 */
	/*
	 *	compute the new block allocation map configuration
	 *
	 * map dinode:
	 *  di_size: map file size in byte;
	 *  di_nblocks: number of blocks allocated for map file;
	 *  di_mapsize: number of blocks in aggregate (covered by map);
	 * map control page:
	 *  db_mapsize: number of blocks in aggregate (covered by map);
	 */
	newMapSize = newFSSize;
	/* number of data pages of new bmap file:
	 * roundup new size to full dmap page boundary and
	 * add 1 extra dmap page for next extendfs()
	 */
	t64 = (newMapSize - 1) + BPERDMAP;
	newNpages = BLKTODMAPN(t64) + 1;

	/*
	 *	extend map from current map (WITHOUT growing mapfile)
	 *
	 * map new extension with unmapped part of the last partial
	 * dmap page, if applicable, and extra page(s) allocated
	 * at end of bmap by mkfs() or previous extendfs();
	 */
      extendBmap:
	/* compute number of blocks requested to extend */
	mapSize = bmp->db_mapsize;
	XAddress = mapSize;	/* eXtension Address */
	XSize = newMapSize - mapSize;	/* eXtension Size */
	old_agsize = bmp->db_agsize;	/* We need to know if this changes */

	/* compute number of blocks that can be extended by current mapfile */
	t64 = dbMapFileSizeToMapSize(ipbmap);
	if (mapSize > t64) {
		printk(KERN_ERR "jfs_extendfs: mapSize (0x%Lx) > t64 (0x%Lx)\n",
		       (long long) mapSize, (long long) t64);
		rc = -EIO;
		goto error_out;
	}
	nblocks = min(t64 - mapSize, XSize);

	/*
	 * update map pages for new extension:
	 *
	 * update/init dmap and bubble up the control hierarchy
	 * incrementally fold up dmaps into upper levels;
	 * update bmap control page;
	 */
	if ((rc = dbExtendFS(ipbmap, XAddress, nblocks)))
		goto error_out;

	agsizechanged |= (bmp->db_agsize != old_agsize);

	/*
	 * the map now has extended to cover additional nblocks:
	 * dn_mapsize = oldMapsize + nblocks;
	 */
	/* ipbmap->i_mapsize += nblocks; */
	XSize -= nblocks;

	/*
	 *	grow map file to cover remaining extension
	 *	and/or one extra dmap page for next extendfs();
	 *
	 * allocate new map pages and its backing blocks, and
	 * update map file xtree
	 */
	/* compute number of data pages of current bmap file */
	nPages = ipbmap->i_size >> L2PSIZE;

	/* need to grow map file ? */
	if (nPages == newNpages)
		goto finalizeBmap;

	/*
	 * grow bmap file for the new map pages required:
	 *
	 * allocate growth at the start of newly extended region;
	 * bmap file only grows sequentially, i.e., both data pages
	 * and possibly xtree index pages may grow in append mode,
	 * s.t. logredo() can reconstruct pre-extension state
	 * by washing away bmap file of pages outside s_size boundary;
	 */
	/*
	 * journal map file growth as if a regular file growth:
	 * (note: bmap is created with di_mode = IFJOURNAL|IFREG);
	 *
	 * journaling of bmap file growth is not required since
	 * logredo() do/can not use log records of bmap file growth
	 * but it provides careful write semantics, pmap update, etc.;
	 */
	/* synchronous write of data pages: bmap data pages are
	 * cached in meta-data cache, and not written out
	 * by txCommit();
	 */
	rc = filemap_fdatawait(ipbmap->i_mapping);
	if (rc)
		goto error_out;

	rc = filemap_write_and_wait(ipbmap->i_mapping);
	if (rc)
		goto error_out;

	diWriteSpecial(ipbmap, 0);

	newPage = nPages;	/* first new page number */
	xoff = newPage << sbi->l2nbperpage;
	xlen = (newNpages - nPages) << sbi->l2nbperpage;
	xlen = min(xlen, (int) nblocks) & ~(sbi->nbperpage - 1);
	xaddr = XAddress;

	tid = txBegin(sb, COMMIT_FORCE);

	if ((rc = xtAppend(tid, ipbmap, 0, xoff, nblocks, &xlen, &xaddr, 0))) {
		txEnd(tid);
		goto error_out;
	}
	/* update bmap file size */
	ipbmap->i_size += xlen << sbi->l2bsize;
	inode_add_bytes(ipbmap, xlen << sbi->l2bsize);

	iplist[0] = ipbmap;
	rc = txCommit(tid, 1, &iplist[0], COMMIT_FORCE);

	txEnd(tid);

	if (rc)
		goto error_out;

	/*
	 * map file has been grown now to cover extension to further out;
	 * di_size = new map file size;
	 *
	 * if huge extension, the previous extension based on previous
	 * map file size may not have been sufficient to cover whole extension
	 * (it could have been used up for new map pages),
	 * but the newly grown map file now covers lot bigger new free space
	 * available for further extension of map;
	 */
	/* any more blocks to extend ? */
	if (XSize)
		goto extendBmap;

      finalizeBmap:
	/* finalize bmap */
	dbFinalizeBmap(ipbmap);

	/*
	 *	update inode allocation map
	 *	---------------------------
	 *
	 * move iag lists from old to new iag;
	 * agstart field is not updated for logredo() to reconstruct
	 * iag lists if system crash occurs.
	 * (computation of ag number from agstart based on agsize
	 * will correctly identify the new ag);
	 */
	/* if new AG size the same as old AG size, done! */
	if (agsizechanged) {
		if ((rc = diExtendFS(ipimap, ipbmap)))
			goto error_out;

		/* finalize imap */
		if ((rc = diSync(ipimap)))
			goto error_out;
	}

	/*
	 *	finalize
	 *	--------
	 *
	 * extension is committed when on-disk super block is
	 * updated with new descriptors: logredo will recover
	 * crash before it to pre-extension state;
	 */

	/* sync log to skip log replay of bmap file growth transaction; */
	/* lmLogSync(log, 1); */

	/*
	 * synchronous write bmap global control page;
	 * for crash before completion of write
	 * logredo() will recover to pre-extendfs state;
	 * for crash after completion of write,
	 * logredo() will recover post-extendfs state;
	 */
	if ((rc = dbSync(ipbmap)))
		goto error_out;

	/*
	 * copy primary bmap inode to secondary bmap inode
	 */

	ipbmap2 = diReadSpecial(sb, BMAP_I, 1);
	if (ipbmap2 == NULL) {
		printk(KERN_ERR "jfs_extendfs: diReadSpecial(bmap) failed\n");
		goto error_out;
	}
	memcpy(&JFS_IP(ipbmap2)->i_xtroot, &JFS_IP(ipbmap)->i_xtroot, 288);
	ipbmap2->i_size = ipbmap->i_size;
	ipbmap2->i_blocks = ipbmap->i_blocks;

	diWriteSpecial(ipbmap2, 1);
	diFreeSpecial(ipbmap2);

	/*
	 *	update superblock
	 */
	if ((rc = readSuper(sb, &bh)))
		goto error_out;
	j_sb = (struct jfs_superblock *)bh->b_data;

	/* mark extendfs() completion */
	j_sb->s_state &= cpu_to_le32(~FM_EXTENDFS);
	j_sb->s_size = cpu_to_le64(bmp->db_mapsize <<
				   le16_to_cpu(j_sb->s_l2bfactor));
	j_sb->s_agsize = cpu_to_le32(bmp->db_agsize);

	/* update inline log space descriptor */
	if (sbi->mntflag & JFS_INLINELOG) {
		PXDaddress(&(j_sb->s_logpxd), newLogAddress);
		PXDlength(&(j_sb->s_logpxd), newLogSize);
	}

	/* record log's mount serial number */
	j_sb->s_logserial = cpu_to_le32(log->serial);

	/* update fsck work space descriptor */
	PXDaddress(&(j_sb->s_fsckpxd), newFSCKAddress);
	PXDlength(&(j_sb->s_fsckpxd), newFSCKSize);
	j_sb->s_fscklog = 1;
	/* sb->s_fsckloglen remains the same */

	/* Update secondary superblock */
	bh2 = sb_bread(sb, SUPER2_OFF >> sb->s_blocksize_bits);
	if (bh2) {
		j_sb2 = (struct jfs_superblock *)bh2->b_data;
		memcpy(j_sb2, j_sb, sizeof (struct jfs_superblock));

		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh2);
		brelse(bh2);
	}

	/* write primary superblock */
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	goto resume;

      error_out:
	jfs_error(sb, "\n");

      resume:
	/*
	 *	resume file system transactions
	 */
	txResume(sb);

      out:
	return rc;
}
