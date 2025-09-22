/*	$OpenBSD: msdosfsmount.h,v 1.23 2024/05/13 01:15:53 jsg Exp $	*/
/*	$NetBSD: msdosfsmount.h,v 1.16 1997/10/17 11:24:24 ws Exp $	*/

/*-
 * Copyright (C) 1994, 1995, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1995, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com) (see below).
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */

/*
 * Layout of the mount control block for a msdos file system.
 */
struct msdosfsmount {
	struct mount *pm_mountp;/* vfs mount struct for this fs */
	dev_t pm_dev;		/* block special device mounted */
	uid_t pm_uid;		/* uid to set as owner of the files */
	gid_t pm_gid;		/* gid to set as owner of the files */
	mode_t pm_mask;		/* mask to and with file protection bits */
	struct vnode *pm_devvp;	/* vnode for block device mntd */
	struct bpb50 pm_bpb;	/* BIOS parameter blk for this fs */
	uint32_t pm_BlkPerSec;	/* # of DEV_BSIZE blocks in MSDOSFS sector */
	uint32_t pm_FATsecs;	/* actual number of fat sectors */
	uint32_t pm_fatblk;	/* block # of first FAT */
	uint32_t pm_rootdirblk;	/* block # (cluster # for FAT32) of root directory number */
	uint32_t pm_rootdirsize;	/* size in blocks (not clusters) */
	uint32_t pm_firstcluster;	/* block number of first cluster */
	uint32_t pm_nmbrofclusters;	/* # of clusters in filesystem */
	uint32_t pm_maxcluster;	/* maximum cluster number */
	uint32_t pm_freeclustercount;	/* number of free clusters */
	uint32_t pm_cnshift;	/* shift file offset right this amount to get a cluster number */
	uint32_t pm_crbomask;	/* and a file offset with this mask to get cluster rel offset */
	uint32_t pm_bnshift;	/* shift file offset right this amount to get a block number */
	uint32_t pm_bpcluster;	/* bytes per cluster */
	uint32_t pm_fmod;		/* ~0 if fs is modified, this can rollover to 0	*/
	uint32_t pm_fatblocksize;	/* size of fat blocks in bytes */
	uint32_t pm_fatblocksec;	/* size of fat blocks in sectors */
	uint32_t pm_fatsize;	/* size of fat in bytes */
	uint32_t pm_fatmask;	/* mask to use for fat numbers */
	uint32_t pm_fsinfo;	/* fsinfo block number */
	u_int pm_fatmult;	/* these 2 values are used in fat */
	u_int pm_fatdiv;	/*	offset computation */
	u_int pm_curfat;	/* current fat for FAT32 (0 otherwise) */
	u_int *pm_inusemap;	/* ptr to bitmap of in-use clusters */
	u_int pm_flags;		/* see below */
	struct netexport pm_export;	/* export information */
};
/* Byte offset in FAT on filesystem pmp, cluster cn */
#define	FATOFS(pmp, cn)	((cn) * (pmp)->pm_fatmult / (pmp)->pm_fatdiv)

/*
 * Mount point flags:
 */
#if 0
    /* Defined in <sys/mount.h> */
#define	MSDOSFSMNT_SHORTNAME	0x01
#define	MSDOSFSMNT_LONGNAME	0x02
#define	MSDOSFSMNT_NOWIN95	0x04
#endif

/* All flags above: */
#define	MSDOSFSMNT_MNTOPT \
	(MSDOSFSMNT_SHORTNAME|MSDOSFSMNT_LONGNAME|MSDOSFSMNT_NOWIN95)
#define	MSDOSFSMNT_RONLY	0x80000000	/* mounted read-only	*/
#define	MSDOSFSMNT_WAITONFAT	0x40000000	/* mounted synchronous	*/
#define	MSDOSFS_FATMIRROR	0x20000000	/* FAT is mirrored */

#define	VFSTOMSDOSFS(mp)	((struct msdosfsmount *)mp->mnt_data)

/* Number of bits in one pm_inusemap item: */
#define	N_INUSEBITS	(8 * sizeof(u_int))

/*
 * Shorthand for fields in the bpb contained in the msdosfsmount structure.
 */
#define	pm_BytesPerSec	pm_bpb.bpbBytesPerSec
#define	pm_ResSectors	pm_bpb.bpbResSectors
#define	pm_FATs		pm_bpb.bpbFATs
#define	pm_RootDirEnts	pm_bpb.bpbRootDirEnts
#define	pm_Sectors	pm_bpb.bpbSectors
#define	pm_Media	pm_bpb.bpbMedia
#define	pm_SecPerTrack	pm_bpb.bpbSecPerTrack
#define	pm_Heads	pm_bpb.bpbHeads
#define	pm_HiddenSects	pm_bpb.bpbHiddenSecs
#define	pm_HugeSectors	pm_bpb.bpbHugeSectors

/*
 * Convert pointer to buffer -> pointer to direntry
 */
#define	bptoep(pmp, bp, dirofs) \
	((struct direntry *)(((bp)->b_data)	\
	 + ((dirofs) & (pmp)->pm_crbomask)))

/*
 * Convert block number to cluster number
 */
#define	de_bn2cn(pmp, bn) \
	((bn) >> ((pmp)->pm_cnshift - (pmp)->pm_bnshift))

/*
 * Convert cluster number to block number
 */
#define	de_cn2bn(pmp, cn) \
	((cn) << ((pmp)->pm_cnshift - (pmp)->pm_bnshift))

/*
 * Convert file offset to cluster number
 */
#define de_cluster(pmp, off) \
	((off) >> (pmp)->pm_cnshift)

/*
 * Clusters required to hold size bytes
 */
#define	de_clcount(pmp, size) \
	(((size) + (pmp)->pm_bpcluster - 1) >> (pmp)->pm_cnshift)

/*
 * Convert file offset to block number
 */
#define de_blk(pmp, off) \
	(de_cn2bn(pmp, de_cluster((pmp), (off))))

/*
 * Convert cluster number to file offset
 */
#define	de_cn2off(pmp, cn) \
	((cn) << (pmp)->pm_cnshift)

/*
 * Convert block number to file offset
 */
#define	de_bn2off(pmp, bn) \
	((bn) << (pmp)->pm_bnshift)
/*
 * Map a cluster number into a filesystem relative block number.
 */
#define	cntobn(pmp, cn) \
	(de_cn2bn((pmp), (cn)-CLUST_FIRST) + (pmp)->pm_firstcluster)

/*
 * Calculate block number for directory entry in root dir, offset dirofs
 */
#define	roottobn(pmp, dirofs) \
	(de_blk((pmp), (dirofs)) + (pmp)->pm_rootdirblk)

/*
 * Calculate block number for directory entry at cluster dirclu, offset
 * dirofs
 */
#define	detobn(pmp, dirclu, dirofs) \
	((dirclu) == MSDOSFSROOT \
	 ? roottobn((pmp), (dirofs)) \
	 : cntobn((pmp), (dirclu)))

/* Calculate size of fsinfo block */
#define fsi_size(pmp) \
	(1024 << ((pmp)->pm_BlkPerSec >> 2))

/*
 * Prototypes for MSDOSFS virtual filesystem operations
 */
int msdosfs_mount(struct mount *, const char *, void *, struct nameidata *, struct proc *);
int msdosfs_start(struct mount *, int, struct proc *);
int msdosfs_unmount(struct mount *, int, struct proc *);
int msdosfs_root(struct mount *, struct vnode **);
int msdosfs_statfs(struct mount *, struct statfs *, struct proc *);
int msdosfs_sync(struct mount *, int, int, struct ucred *, struct proc *);
int msdosfs_fhtovp(struct mount *, struct fid *, struct vnode **);
int msdosfs_vptofh(struct vnode *, struct fid *);
int msdosfs_init(struct vfsconf *);
