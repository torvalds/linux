/*	$OpenBSD: ext2fs_bswap.c,v 1.8 2014/07/31 17:37:52 pelikan Exp $	*/
/*	$NetBSD: ext2fs_bswap.c,v 1.6 2000/07/24 00:23:10 mycroft Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#if defined(_KERNEL)
#include <sys/systm.h>
#endif

#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ext2fs/ext2fs_dinode.h>

#if !defined(_KERNEL)
#include <string.h>
#endif

/* These functions are only needed if native byte order is not big endian */
#if BYTE_ORDER == BIG_ENDIAN
void
e2fs_sb_bswap(struct ext2fs *old, struct ext2fs *new)
{
	/* preserve unused fields */
	memcpy(new, old, sizeof(struct ext2fs));
	new->e2fs_icount	=	swap32(old->e2fs_icount);
	new->e2fs_bcount	=	swap32(old->e2fs_bcount);
	new->e2fs_rbcount	=	swap32(old->e2fs_rbcount);
	new->e2fs_fbcount	=	swap32(old->e2fs_fbcount);
	new->e2fs_ficount	=	swap32(old->e2fs_ficount);
	new->e2fs_first_dblock	=	swap32(old->e2fs_first_dblock);
	new->e2fs_log_bsize	=	swap32(old->e2fs_log_bsize);
	new->e2fs_log_fsize	=	swap32(old->e2fs_log_fsize);
	new->e2fs_bpg		=	swap32(old->e2fs_bpg);
	new->e2fs_fpg		=	swap32(old->e2fs_fpg);
	new->e2fs_ipg		=	swap32(old->e2fs_ipg);
	new->e2fs_mtime		=	swap32(old->e2fs_mtime);
	new->e2fs_wtime		=	swap32(old->e2fs_wtime);
	new->e2fs_mnt_count	=	swap16(old->e2fs_mnt_count);
	new->e2fs_max_mnt_count	=	swap16(old->e2fs_max_mnt_count);
	new->e2fs_magic		=	swap16(old->e2fs_magic);
	new->e2fs_state		=	swap16(old->e2fs_state);
	new->e2fs_beh		=	swap16(old->e2fs_beh);
	new->e2fs_minrev	=	swap16(old->e2fs_minrev);
	new->e2fs_lastfsck	=	swap32(old->e2fs_lastfsck);
	new->e2fs_fsckintv	=	swap32(old->e2fs_fsckintv);
	new->e2fs_creator	=	swap32(old->e2fs_creator);
	new->e2fs_rev		=	swap32(old->e2fs_rev);
	new->e2fs_ruid		=	swap16(old->e2fs_ruid);
	new->e2fs_rgid		=	swap16(old->e2fs_rgid);
	new->e2fs_first_ino	=	swap32(old->e2fs_first_ino);
	new->e2fs_inode_size	=	swap16(old->e2fs_inode_size);
	new->e2fs_block_group_nr =	swap16(old->e2fs_block_group_nr);
	new->e2fs_features_compat =	swap32(old->e2fs_features_compat);
	new->e2fs_features_incompat =	swap32(old->e2fs_features_incompat);
	new->e2fs_features_rocompat =	swap32(old->e2fs_features_rocompat);
	new->e2fs_algo		=	swap32(old->e2fs_algo);

	/* SOME journaling-related fields. */
	new->e2fs_journal_ino	=	swap32(old->e2fs_journal_ino);
	new->e2fs_journal_dev	=	swap32(old->e2fs_journal_dev);
	new->e2fs_last_orphan	=	swap32(old->e2fs_last_orphan);
	new->e2fs_gdesc_size	=	swap16(old->e2fs_gdesc_size);
	new->e2fs_default_mount_opts	=	swap32(old->e2fs_default_mount_opts);
	new->e2fs_first_meta_bg	=	swap32(old->e2fs_first_meta_bg);
	new->e2fs_mkfs_time	=	swap32(old->e2fs_mkfs_time);
}

void
e2fs_cg_bswap(struct ext2_gd *old, struct ext2_gd *new, int size)
{
	int i;
	for (i=0; i < (size / sizeof(struct  ext2_gd)); i++) {
		new[i].ext2bgd_b_bitmap	= swap32(old[i].ext2bgd_b_bitmap);
		new[i].ext2bgd_i_bitmap	= swap32(old[i].ext2bgd_i_bitmap);
		new[i].ext2bgd_i_tables	= swap32(old[i].ext2bgd_i_tables);
		new[i].ext2bgd_nbfree	= swap16(old[i].ext2bgd_nbfree);
		new[i].ext2bgd_nifree	= swap16(old[i].ext2bgd_nifree);
		new[i].ext2bgd_ndirs	= swap16(old[i].ext2bgd_ndirs);
	}
}

void
e2fs_i_bswap(struct m_ext2fs *fs, struct ext2fs_dinode *old,
    struct ext2fs_dinode *new)
{
	new->e2di_mode		=	swap16(old->e2di_mode);
	new->e2di_uid_low	=	swap16(old->e2di_uid_low);
	new->e2di_gid_low	=	swap16(old->e2di_gid_low);
	new->e2di_uid_high	=	swap16(old->e2di_uid_high);
	new->e2di_gid_high	=	swap16(old->e2di_gid_high);
	new->e2di_nlink		=	swap16(old->e2di_nlink);
	new->e2di_size		=	swap32(old->e2di_size);
	new->e2di_atime		=	swap32(old->e2di_atime);
	new->e2di_ctime		=	swap32(old->e2di_ctime);
	new->e2di_mtime		=	swap32(old->e2di_mtime);
	new->e2di_dtime		=	swap32(old->e2di_dtime);
	new->e2di_nblock	=	swap32(old->e2di_nblock);
	new->e2di_flags		=	swap32(old->e2di_flags);
	new->e2di_gen		=	swap32(old->e2di_gen);
	new->e2di_facl		=	swap32(old->e2di_facl);
	new->e2di_size_hi	=	swap32(old->e2di_size_hi);
	new->e2di_faddr		=	swap32(old->e2di_faddr);
	new->e2di_nblock_hi	=	swap16(old->e2di_nblock_hi);
	new->e2di_facl_hi	=	swap16(old->e2di_facl_hi);
	memcpy(&new->e2di_blocks[0], &old->e2di_blocks[0],
		(NDADDR+NIADDR) * sizeof(int));

	if (EXT2_DINODE_SIZE(fs) <= EXT2_REV0_DINODE_SIZE)
		return;
	new->e2di_isize		=	swap16(old->e2di_isize);
}
#endif
