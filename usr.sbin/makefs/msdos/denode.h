/*	$OpenBSD: denode.h,v 1.2 2016/12/17 16:43:30 krw Exp $	*/
/*	$NetBSD: denode.h,v 1.24 2014/07/08 09:21:52 hannken Exp $	*/

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
#ifndef _MSDOSFS_DENODE_H_
#define _MSDOSFS_DENODE_H_

struct genfs_node {
};
struct mkfsvnode;
struct msdosfsmount;
struct mkfsbuf;

/*
 * This is the pc filesystem specific portion of the mkfsvnode structure.
 *
 * To describe a file uniquely the de_dirclust, de_diroffset, and
 * de_StartCluster fields are used.
 *
 * de_dirclust contains the cluster number of the directory cluster
 *	containing the entry for a file or directory.
 * de_diroffset is the index into the cluster for the entry describing
 *	a file or directory.
 * de_StartCluster is the number of the first cluster of the file or directory.
 *
 * Now to describe the quirks of the pc filesystem.
 * - Clusters 0 and 1 are reserved.
 * - The first allocatable cluster is 2.
 * - The root directory is of fixed size and all blocks that make it up
 *   are contiguous.
 * - Cluster 0 refers to the root directory when it is found in the
 *   startcluster field of a directory entry that points to another directory.
 * - Cluster 0 implies a 0 length file when found in the start cluster field
 *   of a directory entry that points to a file.
 * - You can't use the cluster number 0 to derive the address of the root
 *   directory.
 * - Multiple directory entries can point to a directory. The entry in the
 *   parent directory points to a child directory.  Any directories in the
 *   child directory contain a ".." entry that points back to the parent.
 *   The child directory itself contains a "." entry that points to itself.
 * - The root directory does not contain a "." or ".." entry.
 * - Directory entries for directories are never changed once they are created
 *   (except when removed).  The size stays 0, and the last modification time
 *   is never changed.  This is because so many directory entries can point to
 *   the physical clusters that make up a directory.  It would lead to an
 *   update nightmare.
 * - The length field in a directory entry pointing to a directory contains 0
 *   (always).  The only way to find the end of a directory is to follow the
 *   cluster chain until the "last cluster" marker is found.
 *
 * My extensions to make this house of cards work.  These apply only to the in
 * memory copy of the directory entry.
 * - A reference count for each denode will be kept since dos doesn't keep such
 *   things.
 */

/*
 * Internal pseudo-offset for (nonexistent) directory entry for the root
 * dir in the root dir
 */
#define	MSDOSFSROOT_OFS	0x1fffffff

/*
 * The FAT cache structure. fc_fsrcn is the filesystem relative cluster
 * number that corresponds to the file relative cluster number in this
 * structure (fc_frcn).
 */
struct fatcache {
	u_long fc_frcn;		/* file relative cluster number */
	u_long fc_fsrcn;	/* filesystem relative cluster number */
};

/*
 * The FAT entry cache as it stands helps make extending files a "quick"
 * operation by avoiding having to scan the FAT to discover the last
 * cluster of the file. The cache also helps sequential reads by
 * remembering the last cluster read from the file.  This also prevents us
 * from having to rescan the FAT to find the next cluster to read.  This
 * cache is probably pretty worthless if a file is opened by multiple
 * processes.
 */
#define	FC_SIZE		3	/* number of entries in the cache */
#define	FC_LASTMAP	0	/* entry the last call to pcbmap() resolved
				 * to */
#define	FC_LASTFC	1	/* entry for the last cluster in the file */
#define	FC_NEXTTOLASTFC	2	/* entry for a close to the last cluster in the file */

#define	FCE_EMPTY	0xffffffff	/* doesn't represent an actual cluster # */

/*
 * Set a slot in the FAT cache.
 */
#define	fc_setcache(dep, slot, frcn, fsrcn) \
	(dep)->de_fc[slot].fc_frcn = frcn; \
	(dep)->de_fc[slot].fc_fsrcn = fsrcn;

#define fc_last_to_nexttolast(dep) \
	do {  \
		(dep)->de_fc[FC_NEXTTOLASTFC].fc_frcn = (dep)->de_fc[FC_LASTFC].fc_frcn; \
		(dep)->de_fc[FC_NEXTTOLASTFC].fc_fsrcn = (dep)->de_fc[FC_LASTFC].fc_fsrcn; \
	} while (0)


/*
 * This is the in memory variant of a dos directory entry.  It is usually
 * contained within a mkfsvnode.
 */
struct denode_key {
	u_long dk_dirclust;	/* cluster of the directory file containing this entry */
	u_long dk_diroffset;	/* offset of this entry in the directory cluster */
	void *dk_dirgen;	/* non zero and unique for unlinked nodes */
};
struct denode {
	struct genfs_node de_gnode;
	struct mkfsvnode *de_mkfsvnode;	/* addr of mkfsvnode we are part of */
	struct mkfsvnode *de_devvp;	/* mkfsvnode of blk dev we live on */
	u_long de_flag;		/* flag bits */
	dev_t de_dev;		/* device where direntry lives */
	struct denode_key de_key;
#define de_dirclust de_key.dk_dirclust
#define de_diroffset de_key.dk_diroffset
#define de_dirgen de_key.dk_dirgen
	u_long de_fndoffset;	/* offset of found dir entry */
	int de_fndcnt;		/* number of slots before de_fndoffset */
	long de_refcnt;		/* reference count */
	struct msdosfsmount *de_pmp;	/* addr of our mount struct */
	struct lockf *de_lockf;	/* byte level lock list */
	u_char de_Name[12];	/* name, from DOS directory entry */
	u_char de_Attributes;	/* attributes, from directory entry */
	u_char de_CHun;		/* Hundredth of second of CTime*/
	u_short de_CTime;	/* creation time */
	u_short de_CDate;	/* creation date */
	u_short de_ADate;	/* access date */
	u_short de_MTime;	/* modification time */
	u_short de_MDate;	/* modification date */
	u_long de_StartCluster; /* starting cluster of file */
	u_long de_FileSize;	/* size of file in bytes */
	struct fatcache de_fc[FC_SIZE];	/* FAT cache */
};

/*
 * Values for the de_flag field of the denode.
 */
#define	DE_UPDATE	0x0001	/* Modification time update request. */
#define	DE_CREATE	0x0002	/* Creation time update */
#define	DE_ACCESS	0x0004	/* Access time update */
#define	DE_MODIFIED	0x0008	/* Denode has been modified. */
#define	DE_RENAME	0x0010	/* Denode is in the process of being renamed */

/*
 * Maximum filename length in Win95
 * Note: Must be < sizeof(dirent.d_name)
 */
#define	WIN_MAXLEN	255

/* Maximum size of a file on a FAT filesystem */
#define MSDOSFS_FILESIZE_MAX	0xFFFFFFFFLL

/*
 * Transfer directory entries between internal and external form.
 * dep is a struct denode * (internal form),
 * dp is a struct direntry * (external form).
 */
#define DE_INTERNALIZE32(dep, dp)			\
	 ((dep)->de_StartCluster |= getushort((dp)->deHighClust) << 16)
#define DE_INTERNALIZE(dep, dp)				\
	(memcpy((dep)->de_Name, (dp)->deName, 11),	\
	 (dep)->de_Attributes = (dp)->deAttributes,	\
	 (dep)->de_CHun = (dp)->deCTimeHundredth,	\
	 (dep)->de_CTime = getushort((dp)->deCTime),	\
	 (dep)->de_CDate = getushort((dp)->deCDate),	\
	 (dep)->de_ADate = getushort((dp)->deADate),	\
	 (dep)->de_MTime = getushort((dp)->deMTime),	\
	 (dep)->de_MDate = getushort((dp)->deMDate),	\
	 (dep)->de_StartCluster = getushort((dp)->deStartCluster), \
	 (dep)->de_FileSize = getulong((dp)->deFileSize), \
	 (FAT32((dep)->de_pmp) ? DE_INTERNALIZE32((dep), (dp)) : 0))

#define DE_EXTERNALIZE32(dp, dep)			\
	 putushort((dp)->deHighClust, (dep)->de_StartCluster >> 16)
#define DE_EXTERNALIZE16(dp, dep)			\
	 putushort((dp)->deHighClust, 0)
#define DE_EXTERNALIZE(dp, dep)				\
	(memcpy((dp)->deName, (dep)->de_Name, 11),	\
	 (dp)->deAttributes = (dep)->de_Attributes,	\
	 (dp)->deCTimeHundredth = (dep)->de_CHun,	\
	 putushort((dp)->deCTime, (dep)->de_CTime),	\
	 putushort((dp)->deCDate, (dep)->de_CDate),	\
	 putushort((dp)->deADate, (dep)->de_ADate),	\
	 putushort((dp)->deMTime, (dep)->de_MTime),	\
	 putushort((dp)->deMDate, (dep)->de_MDate),	\
	 putushort((dp)->deStartCluster, (dep)->de_StartCluster), \
	 putulong((dp)->deFileSize,			\
	     ((dep)->de_Attributes & ATTR_DIRECTORY) ? 0 : (dep)->de_FileSize), \
	 (FAT32((dep)->de_pmp) ? DE_EXTERNALIZE32((dp), (dep)) : DE_EXTERNALIZE16((dp), (dep))))

#define	de_forw		de_chain[0]
#define	de_back		de_chain[1]


#define	VTODE(vp)	((struct denode *)(vp)->v_data)
#define	DETOV(de)	((de)->de_mkfsvnode)

#define	DETIMES(dep, acc, mod, cre, gmtoff) \
	while ((dep)->de_flag & (DE_UPDATE | DE_CREATE | DE_ACCESS)) \
		msdosfs_detimes(dep, acc, mod, cre, gmtoff)

/*
 * This overlays the fid structure (see fstypes.h)
 */
struct defid {
	u_int16_t defid_len;	/* length of structure */
	u_int16_t defid_pad;	/* force 4-byte alignment */

	u_int32_t defid_dirclust; /* cluster this dir entry came from */
	u_int32_t defid_dirofs;	/* offset of entry within the cluster */
	u_int32_t defid_gen;	/* generation number */
};

/*
 * Prototypes for MSDOSFS mkfsvnode operations
 */
int	msdosfs_lookup		(void *);
int	msdosfs_create		(void *);
int	msdosfs_close		(void *);
int	msdosfs_access		(void *);
int	msdosfs_getattr		(void *);
int	msdosfs_setattr		(void *);
int	msdosfs_read		(void *);
int	msdosfs_write		(void *);
#define	msdosfs_lease_check	genfs_lease_check
#define	msdosfs_ioctl		genfs_enoioctl
#define	msdosfs_poll		genfs_poll
#define	msdosfs_revoke		genfs_revoke
#define	msdosfs_mmap		genfs_mmap
int	msdosfs_fsync		(void *);
#define	msdosfs_seek		genfs_seek
int	msdosfs_remove		(void *);
int	msdosfs_rename		(void *);
int	msdosfs_mkdir		(void *);
int	msdosfs_rmdir		(void *);
int	msdosfs_readdir		(void *);
#define	msdosfs_abortop		genfs_abortop
int	msdosfs_inactive	(void *);
int	msdosfs_reclaim		(void *);
int	msdosfs_bmap		(void *);
int	msdosfs_strategy	(void *);
int	msdosfs_print		(void *);
int	msdosfs_advlock		(void *);
int	msdosfs_pathconf	(void *);

/*
 * Internal service routine prototypes.
 */
struct componentname;
struct direntry;
int msdosfs_update(struct mkfsvnode *, const struct timespec *,
	    const struct timespec *, int);
int createde(struct denode *, struct denode *,
		struct denode **, struct componentname *);
int deextend(struct denode *, u_long);
int deget(struct msdosfsmount *, u_long, u_long, struct denode **);
int detrunc(struct denode *, u_long, int);
int deupdat(struct denode *, int);
int readde(struct denode *, struct mkfsbuf **, struct direntry **);
int readep(struct msdosfsmount *, u_long, u_long,
		struct mkfsbuf **, struct direntry **);
int uniqdosname(struct denode *, struct componentname *, u_char *);
int msdosfs_gop_alloc(struct mkfsvnode *, off_t, off_t, int);
void msdosfs_gop_markupdate(struct mkfsvnode *, int);
void msdosfs_detimes(struct denode *, const struct timespec *,
    const struct timespec *, const struct timespec *, int);
int msdosfs_fh_enter(struct msdosfsmount *, uint32_t, uint32_t, uint32_t *);
int msdosfs_fh_remove(struct msdosfsmount *, uint32_t, uint32_t);
int msdosfs_fh_lookup(struct msdosfsmount *, uint32_t, uint32_t, uint32_t *);
void msdosfs_fh_destroy(struct msdosfsmount *);
#endif /* _MSDOSFS_DENODE_H_ */
