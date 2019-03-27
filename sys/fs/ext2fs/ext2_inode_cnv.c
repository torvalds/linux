/*-
 * SPDX-License-Identifier: MIT-CMU
 *
 * Copyright (c) 1995 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 *      Utah $Hdr$
 * $FreeBSD$
 */

/*
 * routines to convert on disk ext2 inodes into inodes and back
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/stat.h>
#include <sys/vnode.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_extern.h>

#define XTIME_TO_NSEC(x)	((x & EXT3_NSEC_MASK) >> 2)
#define NSEC_TO_XTIME(t)	(le32toh(t << 2) & EXT3_NSEC_MASK)

#ifdef EXT2FS_DEBUG
void
ext2_print_inode(struct inode *in)
{
	int i;
	struct ext4_extent_header *ehp;
	struct ext4_extent *ep;

	printf("Inode: %5ju", (uintmax_t)in->i_number);
	printf(	/* "Inode: %5d" */
	    " Type: %10s Mode: 0x%o Flags: 0x%x  Version: %d acl: 0x%jx\n",
	    "n/a", in->i_mode, in->i_flags, in->i_gen, in->i_facl);
	printf("User: %5u Group: %5u  Size: %ju\n",
	    in->i_uid, in->i_gid, (uintmax_t)in->i_size);
	printf("Links: %3d Blockcount: %ju\n",
	    in->i_nlink, (uintmax_t)in->i_blocks);
	printf("ctime: 0x%x ", in->i_ctime);
	printf("atime: 0x%x ", in->i_atime);
	printf("mtime: 0x%x ", in->i_mtime);
	if (E2DI_HAS_XTIME(in))
		printf("crtime %#x\n", in->i_birthtime);
	else
		printf("\n");
	if (in->i_flag & IN_E4EXTENTS) {
		printf("Extents:\n");
		ehp = (struct ext4_extent_header *)in->i_db;
		printf("Header (magic 0x%x entries %d max %d depth %d gen %d)\n",
		    ehp->eh_magic, ehp->eh_ecount, ehp->eh_max, ehp->eh_depth,
		    ehp->eh_gen);
		ep = (struct ext4_extent *)(char *)(ehp + 1);
		printf("Index (blk %d len %d start_lo %d start_hi %d)\n", ep->e_blk,
		    ep->e_len, ep->e_start_lo, ep->e_start_hi);
		printf("\n");
	} else {
		printf("BLOCKS:");
		for (i = 0; i < (in->i_blocks <= 24 ? (in->i_blocks + 1) / 2 : 12); i++)
			printf("  %d", in->i_db[i]);
		printf("\n");
	}
}
#endif	/* EXT2FS_DEBUG */

/*
 *	raw ext2 inode to inode
 */
int
ext2_ei2i(struct ext2fs_dinode *ei, struct inode *ip)
{
	struct m_ext2fs *fs = ip->i_e2fs;

	if ((ip->i_number < EXT2_FIRST_INO(fs) && ip->i_number != EXT2_ROOTINO) ||
	    (ip->i_number < EXT2_ROOTINO) ||
	    (ip->i_number > fs->e2fs->e2fs_icount)) {
		printf("ext2fs: bad inode number %ju\n", ip->i_number);
		return (EINVAL);
	}

	if (ip->i_number == EXT2_ROOTINO && ei->e2di_nlink == 0) {
		printf("ext2fs: root inode unallocated\n");
		return (EINVAL);
	}
	ip->i_nlink = ei->e2di_nlink;

	/* Check extra inode size */
	if (EXT2_INODE_SIZE(fs) > E2FS_REV0_INODE_SIZE) {
		if (E2FS_REV0_INODE_SIZE + ei->e2di_extra_isize >
		    EXT2_INODE_SIZE(fs) || (ei->e2di_extra_isize & 3)) {
			printf("ext2fs: bad extra inode size %u, inode size=%u\n",
			    ei->e2di_extra_isize, EXT2_INODE_SIZE(fs));
			return (EINVAL);
		}
	}

	/*
	 * Godmar thinks - if the link count is zero, then the inode is
	 * unused - according to ext2 standards. Ufs marks this fact by
	 * setting i_mode to zero - why ? I can see that this might lead to
	 * problems in an undelete.
	 */
	ip->i_mode = ei->e2di_nlink ? ei->e2di_mode : 0;
	ip->i_size = ei->e2di_size;
	if (S_ISREG(ip->i_mode))
		ip->i_size |= ((u_int64_t)ei->e2di_size_high) << 32;
	ip->i_atime = ei->e2di_atime;
	ip->i_mtime = ei->e2di_mtime;
	ip->i_ctime = ei->e2di_ctime;
	if (E2DI_HAS_XTIME(ip)) {
		ip->i_atimensec = XTIME_TO_NSEC(ei->e2di_atime_extra);
		ip->i_mtimensec = XTIME_TO_NSEC(ei->e2di_mtime_extra);
		ip->i_ctimensec = XTIME_TO_NSEC(ei->e2di_ctime_extra);
		ip->i_birthtime = ei->e2di_crtime;
		ip->i_birthnsec = XTIME_TO_NSEC(ei->e2di_crtime_extra);
	}
	ip->i_flags = 0;
	ip->i_flags |= (ei->e2di_flags & EXT2_APPEND) ? SF_APPEND : 0;
	ip->i_flags |= (ei->e2di_flags & EXT2_IMMUTABLE) ? SF_IMMUTABLE : 0;
	ip->i_flags |= (ei->e2di_flags & EXT2_NODUMP) ? UF_NODUMP : 0;
	ip->i_flag |= (ei->e2di_flags & EXT3_INDEX) ? IN_E3INDEX : 0;
	ip->i_flag |= (ei->e2di_flags & EXT4_EXTENTS) ? IN_E4EXTENTS : 0;
	ip->i_blocks = ei->e2di_nblock;
	ip->i_facl = ei->e2di_facl;
	if (E2DI_HAS_HUGE_FILE(ip)) {
		ip->i_blocks |= (uint64_t)ei->e2di_nblock_high << 32;
		ip->i_facl |= (uint64_t)ei->e2di_facl_high << 32;
		if (ei->e2di_flags & EXT4_HUGE_FILE)
			ip->i_blocks = fsbtodb(ip->i_e2fs, ip->i_blocks);
	}
	ip->i_gen = ei->e2di_gen;
	ip->i_uid = ei->e2di_uid;
	ip->i_gid = ei->e2di_gid;
	ip->i_uid |= (uint32_t)ei->e2di_uid_high << 16;
	ip->i_gid |= (uint32_t)ei->e2di_gid_high << 16;

	memcpy(ip->i_data, ei->e2di_blocks, sizeof(ei->e2di_blocks));

	/* Verify inode csum. */
	return (ext2_ei_csum_verify(ip, ei));
}

/*
 *	inode to raw ext2 inode
 */
int
ext2_i2ei(struct inode *ip, struct ext2fs_dinode *ei)
{
	struct m_ext2fs *fs;

	fs = ip->i_e2fs;
	ei->e2di_mode = ip->i_mode;
	ei->e2di_nlink = ip->i_nlink;
	/*
	 * Godmar thinks: if dtime is nonzero, ext2 says this inode has been
	 * deleted, this would correspond to a zero link count
	 */
	ei->e2di_dtime = ei->e2di_nlink ? 0 : ip->i_mtime;
	ei->e2di_size = ip->i_size;
	if (S_ISREG(ip->i_mode))
		ei->e2di_size_high = ip->i_size >> 32;
	ei->e2di_atime = ip->i_atime;
	ei->e2di_mtime = ip->i_mtime;
	ei->e2di_ctime = ip->i_ctime;
	if (E2DI_HAS_XTIME(ip)) {
		ei->e2di_ctime_extra = NSEC_TO_XTIME(ip->i_ctimensec);
		ei->e2di_mtime_extra = NSEC_TO_XTIME(ip->i_mtimensec);
		ei->e2di_atime_extra = NSEC_TO_XTIME(ip->i_atimensec);
		ei->e2di_crtime = ip->i_birthtime;
		ei->e2di_crtime_extra = NSEC_TO_XTIME(ip->i_birthnsec);
	}
	ei->e2di_flags = 0;
	ei->e2di_flags |= (ip->i_flags & SF_APPEND) ? EXT2_APPEND : 0;
	ei->e2di_flags |= (ip->i_flags & SF_IMMUTABLE) ? EXT2_IMMUTABLE : 0;
	ei->e2di_flags |= (ip->i_flags & UF_NODUMP) ? EXT2_NODUMP : 0;
	ei->e2di_flags |= (ip->i_flag & IN_E3INDEX) ? EXT3_INDEX : 0;
	ei->e2di_flags |= (ip->i_flag & IN_E4EXTENTS) ? EXT4_EXTENTS : 0;
	if (ip->i_blocks > ~0U &&
	    !EXT2_HAS_RO_COMPAT_FEATURE(fs, EXT2F_ROCOMPAT_HUGE_FILE)) {
		ext2_fserr(fs, ip->i_uid, "i_blocks value is out of range");
		return (EIO);
	}
	if (ip->i_blocks <= 0xffffffffffffULL) {
		ei->e2di_nblock = ip->i_blocks & 0xffffffff;
		ei->e2di_nblock_high = ip->i_blocks >> 32 & 0xffff;
	} else {
		ei->e2di_flags |= EXT4_HUGE_FILE;
		ei->e2di_nblock = dbtofsb(fs, ip->i_blocks);
		ei->e2di_nblock_high = dbtofsb(fs, ip->i_blocks) >> 32 & 0xffff;
	}
	ei->e2di_facl = ip->i_facl & 0xffffffff;
	ei->e2di_facl_high = ip->i_facl >> 32 & 0xffff;
	ei->e2di_gen = ip->i_gen;
	ei->e2di_uid = ip->i_uid & 0xffff;
	ei->e2di_uid_high = ip->i_uid >> 16 & 0xffff;
	ei->e2di_gid = ip->i_gid & 0xffff;
	ei->e2di_gid_high = ip->i_gid >> 16 & 0xffff;

	memcpy(ei->e2di_blocks, ip->i_data, sizeof(ei->e2di_blocks));

	/* Set inode csum. */
	ext2_ei_csum_set(ip, ei);

	return (0);
}
