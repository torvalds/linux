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
 * Module: jfs_mount.c
 *
 * note: file system in transition to aggregate/fileset:
 *
 * file system mount is interpreted as the mount of aggregate,
 * if not already mounted, and mount of the single/only fileset in
 * the aggregate;
 *
 * a file system/aggregate is represented by an internal inode
 * (aka mount inode) initialized with aggregate superblock;
 * each vfs represents a fileset, and points to its "fileset inode
 * allocation map inode" (aka fileset inode):
 * (an aggregate itself is structured recursively as a filset:
 * an internal vfs is constructed and points to its "fileset inode
 * allocation map inode" (aka aggregate inode) where each inode
 * represents a fileset inode) so that inode number is mapped to
 * on-disk inode in uniform way at both aggregate and fileset level;
 *
 * each vnode/inode of a fileset is linked to its vfs (to facilitate
 * per fileset inode operations, e.g., unmount of a fileset, etc.);
 * each inode points to the mount inode (to facilitate access to
 * per aggregate information, e.g., block size, etc.) as well as
 * its file set inode.
 *
 *   aggregate
 *   ipmnt
 *   mntvfs -> fileset ipimap+ -> aggregate ipbmap -> aggregate ipaimap;
 *             fileset vfs     -> vp(1) <-> ... <-> vp(n) <->vproot;
 */

#include <linux/fs.h>
#include <linux/buffer_head.h>

#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_superblock.h"
#include "jfs_dmap.h"
#include "jfs_imap.h"
#include "jfs_metapage.h"
#include "jfs_debug.h"


/*
 * forward references
 */
static int chkSuper(struct super_block *);
static int logMOUNT(struct super_block *sb);

/*
 * NAME:	jfs_mount(sb)
 *
 * FUNCTION:	vfs_mount()
 *
 * PARAMETER:	sb	- super block
 *
 * RETURN:	-EBUSY	- device already mounted or open for write
 *		-EBUSY	- cvrdvp already mounted;
 *		-EBUSY	- mount table full
 *		-ENOTDIR- cvrdvp not directory on a device mount
 *		-ENXIO	- device open failure
 */
int jfs_mount(struct super_block *sb)
{
	int rc = 0;		/* Return code          */
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct inode *ipaimap = NULL;
	struct inode *ipaimap2 = NULL;
	struct inode *ipimap = NULL;
	struct inode *ipbmap = NULL;

	/*
	 * read/validate superblock
	 * (initialize mount inode from the superblock)
	 */
	if ((rc = chkSuper(sb))) {
		goto errout20;
	}

	ipaimap = diReadSpecial(sb, AGGREGATE_I, 0);
	if (ipaimap == NULL) {
		jfs_err("jfs_mount: Faild to read AGGREGATE_I");
		rc = -EIO;
		goto errout20;
	}
	sbi->ipaimap = ipaimap;

	jfs_info("jfs_mount: ipaimap:0x%p", ipaimap);

	/*
	 * initialize aggregate inode allocation map
	 */
	if ((rc = diMount(ipaimap))) {
		jfs_err("jfs_mount: diMount(ipaimap) failed w/rc = %d", rc);
		goto errout21;
	}

	/*
	 * open aggregate block allocation map
	 */
	ipbmap = diReadSpecial(sb, BMAP_I, 0);
	if (ipbmap == NULL) {
		rc = -EIO;
		goto errout22;
	}

	jfs_info("jfs_mount: ipbmap:0x%p", ipbmap);

	sbi->ipbmap = ipbmap;

	/*
	 * initialize aggregate block allocation map
	 */
	if ((rc = dbMount(ipbmap))) {
		jfs_err("jfs_mount: dbMount failed w/rc = %d", rc);
		goto errout22;
	}

	/*
	 * open the secondary aggregate inode allocation map
	 *
	 * This is a duplicate of the aggregate inode allocation map.
	 *
	 * hand craft a vfs in the same fashion as we did to read ipaimap.
	 * By adding INOSPEREXT (32) to the inode number, we are telling
	 * diReadSpecial that we are reading from the secondary aggregate
	 * inode table.  This also creates a unique entry in the inode hash
	 * table.
	 */
	if ((sbi->mntflag & JFS_BAD_SAIT) == 0) {
		ipaimap2 = diReadSpecial(sb, AGGREGATE_I, 1);
		if (ipaimap2 == 0) {
			jfs_err("jfs_mount: Faild to read AGGREGATE_I");
			rc = -EIO;
			goto errout35;
		}
		sbi->ipaimap2 = ipaimap2;

		jfs_info("jfs_mount: ipaimap2:0x%p", ipaimap2);

		/*
		 * initialize secondary aggregate inode allocation map
		 */
		if ((rc = diMount(ipaimap2))) {
			jfs_err("jfs_mount: diMount(ipaimap2) failed, rc = %d",
				rc);
			goto errout35;
		}
	} else
		/* Secondary aggregate inode table is not valid */
		sbi->ipaimap2 = NULL;

	/*
	 *      mount (the only/single) fileset
	 */
	/*
	 * open fileset inode allocation map (aka fileset inode)
	 */
	ipimap = diReadSpecial(sb, FILESYSTEM_I, 0);
	if (ipimap == NULL) {
		jfs_err("jfs_mount: Failed to read FILESYSTEM_I");
		/* open fileset secondary inode allocation map */
		rc = -EIO;
		goto errout40;
	}
	jfs_info("jfs_mount: ipimap:0x%p", ipimap);

	/* map further access of per fileset inodes by the fileset inode */
	sbi->ipimap = ipimap;

	/* initialize fileset inode allocation map */
	if ((rc = diMount(ipimap))) {
		jfs_err("jfs_mount: diMount failed w/rc = %d", rc);
		goto errout41;
	}

	goto out;

	/*
	 *      unwind on error
	 */
      errout41:		/* close fileset inode allocation map inode */
	diFreeSpecial(ipimap);

      errout40:		/* fileset closed */

	/* close secondary aggregate inode allocation map */
	if (ipaimap2) {
		diUnmount(ipaimap2, 1);
		diFreeSpecial(ipaimap2);
	}

      errout35:

	/* close aggregate block allocation map */
	dbUnmount(ipbmap, 1);
	diFreeSpecial(ipbmap);

      errout22:		/* close aggregate inode allocation map */

	diUnmount(ipaimap, 1);

      errout21:		/* close aggregate inodes */
	diFreeSpecial(ipaimap);
      errout20:		/* aggregate closed */

      out:

	if (rc)
		jfs_err("Mount JFS Failure: %d", rc);

	return rc;
}

/*
 * NAME:	jfs_mount_rw(sb, remount)
 *
 * FUNCTION:	Completes read-write mount, or remounts read-only volume
 *		as read-write
 */
int jfs_mount_rw(struct super_block *sb, int remount)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	int rc;

	/*
	 * If we are re-mounting a previously read-only volume, we want to
	 * re-read the inode and block maps, since fsck.jfs may have updated
	 * them.
	 */
	if (remount) {
		if (chkSuper(sb) || (sbi->state != FM_CLEAN))
			return -EINVAL;

		truncate_inode_pages(sbi->ipimap->i_mapping, 0);
		truncate_inode_pages(sbi->ipbmap->i_mapping, 0);
		diUnmount(sbi->ipimap, 1);
		if ((rc = diMount(sbi->ipimap))) {
			jfs_err("jfs_mount_rw: diMount failed!");
			return rc;
		}

		dbUnmount(sbi->ipbmap, 1);
		if ((rc = dbMount(sbi->ipbmap))) {
			jfs_err("jfs_mount_rw: dbMount failed!");
			return rc;
		}
	}

	/*
	 * open/initialize log
	 */
	if ((rc = lmLogOpen(sb)))
		return rc;

	/*
	 * update file system superblock;
	 */
	if ((rc = updateSuper(sb, FM_MOUNT))) {
		jfs_err("jfs_mount: updateSuper failed w/rc = %d", rc);
		lmLogClose(sb);
		return rc;
	}

	/*
	 * write MOUNT log record of the file system
	 */
	logMOUNT(sb);

	return rc;
}

/*
 *	chkSuper()
 *
 * validate the superblock of the file system to be mounted and
 * get the file system parameters.
 *
 * returns
 *	0 with fragsize set if check successful
 *	error code if not successful
 */
static int chkSuper(struct super_block *sb)
{
	int rc = 0;
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct jfs_superblock *j_sb;
	struct buffer_head *bh;
	int AIM_bytesize, AIT_bytesize;
	int expected_AIM_bytesize, expected_AIT_bytesize;
	s64 AIM_byte_addr, AIT_byte_addr, fsckwsp_addr;
	s64 byte_addr_diff0, byte_addr_diff1;
	s32 bsize;

	if ((rc = readSuper(sb, &bh)))
		return rc;
	j_sb = (struct jfs_superblock *)bh->b_data;

	/*
	 * validate superblock
	 */
	/* validate fs signature */
	if (strncmp(j_sb->s_magic, JFS_MAGIC, 4) ||
	    le32_to_cpu(j_sb->s_version) > JFS_VERSION) {
		rc = -EINVAL;
		goto out;
	}

	bsize = le32_to_cpu(j_sb->s_bsize);
#ifdef _JFS_4K
	if (bsize != PSIZE) {
		jfs_err("Currently only 4K block size supported!");
		rc = -EINVAL;
		goto out;
	}
#endif				/* _JFS_4K */

	jfs_info("superblock: flag:0x%08x state:0x%08x size:0x%Lx",
		 le32_to_cpu(j_sb->s_flag), le32_to_cpu(j_sb->s_state),
		 (unsigned long long) le64_to_cpu(j_sb->s_size));

	/* validate the descriptors for Secondary AIM and AIT */
	if ((j_sb->s_flag & cpu_to_le32(JFS_BAD_SAIT)) !=
	    cpu_to_le32(JFS_BAD_SAIT)) {
		expected_AIM_bytesize = 2 * PSIZE;
		AIM_bytesize = lengthPXD(&(j_sb->s_aim2)) * bsize;
		expected_AIT_bytesize = 4 * PSIZE;
		AIT_bytesize = lengthPXD(&(j_sb->s_ait2)) * bsize;
		AIM_byte_addr = addressPXD(&(j_sb->s_aim2)) * bsize;
		AIT_byte_addr = addressPXD(&(j_sb->s_ait2)) * bsize;
		byte_addr_diff0 = AIT_byte_addr - AIM_byte_addr;
		fsckwsp_addr = addressPXD(&(j_sb->s_fsckpxd)) * bsize;
		byte_addr_diff1 = fsckwsp_addr - AIT_byte_addr;
		if ((AIM_bytesize != expected_AIM_bytesize) ||
		    (AIT_bytesize != expected_AIT_bytesize) ||
		    (byte_addr_diff0 != AIM_bytesize) ||
		    (byte_addr_diff1 <= AIT_bytesize))
			j_sb->s_flag |= cpu_to_le32(JFS_BAD_SAIT);
	}

	if ((j_sb->s_flag & cpu_to_le32(JFS_GROUPCOMMIT)) !=
	    cpu_to_le32(JFS_GROUPCOMMIT))
		j_sb->s_flag |= cpu_to_le32(JFS_GROUPCOMMIT);

	/* validate fs state */
	if (j_sb->s_state != cpu_to_le32(FM_CLEAN) &&
	    !(sb->s_flags & MS_RDONLY)) {
		jfs_err("jfs_mount: Mount Failure: File System Dirty.");
		rc = -EINVAL;
		goto out;
	}

	sbi->state = le32_to_cpu(j_sb->s_state);
	sbi->mntflag = le32_to_cpu(j_sb->s_flag);

	/*
	 * JFS always does I/O by 4K pages.  Don't tell the buffer cache
	 * that we use anything else (leave s_blocksize alone).
	 */
	sbi->bsize = bsize;
	sbi->l2bsize = le16_to_cpu(j_sb->s_l2bsize);

	/*
	 * For now, ignore s_pbsize, l2bfactor.  All I/O going through buffer
	 * cache.
	 */
	sbi->nbperpage = PSIZE >> sbi->l2bsize;
	sbi->l2nbperpage = L2PSIZE - sbi->l2bsize;
	sbi->l2niperblk = sbi->l2bsize - L2DISIZE;
	if (sbi->mntflag & JFS_INLINELOG)
		sbi->logpxd = j_sb->s_logpxd;
	else {
		sbi->logdev = new_decode_dev(le32_to_cpu(j_sb->s_logdev));
		memcpy(sbi->uuid, j_sb->s_uuid, sizeof(sbi->uuid));
		memcpy(sbi->loguuid, j_sb->s_loguuid, sizeof(sbi->uuid));
	}
	sbi->fsckpxd = j_sb->s_fsckpxd;
	sbi->ait2 = j_sb->s_ait2;

      out:
	brelse(bh);
	return rc;
}


/*
 *	updateSuper()
 *
 * update synchronously superblock if it is mounted read-write.
 */
int updateSuper(struct super_block *sb, uint state)
{
	struct jfs_superblock *j_sb;
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct buffer_head *bh;
	int rc;

	if (sbi->flag & JFS_NOINTEGRITY) {
		if (state == FM_DIRTY) {
			sbi->p_state = state;
			return 0;
		} else if (state == FM_MOUNT) {
			sbi->p_state = sbi->state;
			state = FM_DIRTY;
		} else if (state == FM_CLEAN) {
			state = sbi->p_state;
		} else
			jfs_err("updateSuper: bad state");
	} else if (sbi->state == FM_DIRTY)
		return 0;

	if ((rc = readSuper(sb, &bh)))
		return rc;

	j_sb = (struct jfs_superblock *)bh->b_data;

	j_sb->s_state = cpu_to_le32(state);
	sbi->state = state;

	if (state == FM_MOUNT) {
		/* record log's dev_t and mount serial number */
		j_sb->s_logdev = cpu_to_le32(new_encode_dev(sbi->log->bdev->bd_dev));
		j_sb->s_logserial = cpu_to_le32(sbi->log->serial);
	} else if (state == FM_CLEAN) {
		/*
		 * If this volume is shared with OS/2, OS/2 will need to
		 * recalculate DASD usage, since we don't deal with it.
		 */
		if (j_sb->s_flag & cpu_to_le32(JFS_DASD_ENABLED))
			j_sb->s_flag |= cpu_to_le32(JFS_DASD_PRIME);
	}

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	return 0;
}


/*
 *	readSuper()
 *
 * read superblock by raw sector address
 */
int readSuper(struct super_block *sb, struct buffer_head **bpp)
{
	/* read in primary superblock */
	*bpp = sb_bread(sb, SUPER1_OFF >> sb->s_blocksize_bits);
	if (*bpp)
		return 0;

	/* read in secondary/replicated superblock */
	*bpp = sb_bread(sb, SUPER2_OFF >> sb->s_blocksize_bits);
	if (*bpp)
		return 0;

	return -EIO;
}


/*
 *	logMOUNT()
 *
 * function: write a MOUNT log record for file system.
 *
 * MOUNT record keeps logredo() from processing log records
 * for this file system past this point in log.
 * it is harmless if mount fails.
 *
 * note: MOUNT record is at aggregate level, not at fileset level,
 * since log records of previous mounts of a fileset
 * (e.g., AFTER record of extent allocation) have to be processed
 * to update block allocation map at aggregate level.
 */
static int logMOUNT(struct super_block *sb)
{
	struct jfs_log *log = JFS_SBI(sb)->log;
	struct lrd lrd;

	lrd.logtid = 0;
	lrd.backchain = 0;
	lrd.type = cpu_to_le16(LOG_MOUNT);
	lrd.length = 0;
	lrd.aggregate = cpu_to_le32(new_encode_dev(sb->s_bdev->bd_dev));
	lmLog(log, NULL, &lrd, NULL);

	return 0;
}
