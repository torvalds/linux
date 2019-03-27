/*	$NetBSD: msdosfs_vnops.c,v 1.19 2017/04/13 17:10:12 christos Exp $ */

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
#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <ffs/buf.h>

#include <fs/msdosfs/bpb.h>
#include <fs/msdosfs/direntry.h>
#include <fs/msdosfs/denode.h>
#include <fs/msdosfs/msdosfsmount.h>
#include <fs/msdosfs/fat.h>

#include "makefs.h"
#include "msdos.h"

#ifdef MSDOSFS_DEBUG
#define DPRINTF(a) printf a
#else
#define DPRINTF(a)
#endif
/*
 * Some general notes:
 *
 * In the ufs filesystem the inodes, superblocks, and indirect blocks are
 * read/written using the vnode for the filesystem. Blocks that represent
 * the contents of a file are read/written using the vnode for the file
 * (including directories when they are read/written as files). This
 * presents problems for the dos filesystem because data that should be in
 * an inode (if dos had them) resides in the directory itself.	Since we
 * must update directory entries without the benefit of having the vnode
 * for the directory we must use the vnode for the filesystem.	This means
 * that when a directory is actually read/written (via read, write, or
 * readdir, or seek) we must use the vnode for the filesystem instead of
 * the vnode for the directory as would happen in ufs. This is to insure we
 * retrieve the correct block from the buffer cache since the hash value is
 * based upon the vnode address and the desired block number.
 */

static int msdosfs_wfile(const char *, struct denode *, fsnode *);

static void
msdosfs_times(struct msdosfsmount *pmp, struct denode *dep,
    const struct stat *st)
{
	struct timespec at;
	struct timespec mt;

	if (stampst.st_ino)
	    st = &stampst;

#ifndef HAVE_NBTOOL_CONFIG_H
	at = st->st_atimespec;
	mt = st->st_mtimespec;
#else
	at.tv_sec = st->st_atime;
	at.tv_nsec = 0;
	mt.tv_sec = st->st_mtime;
	mt.tv_nsec = 0;
#endif
	unix2dostime(&at, pmp->pm_gmtoff, &dep->de_ADate, NULL, NULL);
	unix2dostime(&mt, pmp->pm_gmtoff, &dep->de_MDate, &dep->de_MTime, NULL);
}

/*
 * When we search a directory the blocks containing directory entries are
 * read and examined.  The directory entries contain information that would
 * normally be in the inode of a unix filesystem.  This means that some of
 * a directory's contents may also be in memory resident denodes (sort of
 * an inode).  This can cause problems if we are searching while some other
 * process is modifying a directory.  To prevent one process from accessing
 * incompletely modified directory information we depend upon being the
 * sole owner of a directory block.  bread/brelse provide this service.
 * This being the case, when a process modifies a directory it must first
 * acquire the disk block that contains the directory entry to be modified.
 * Then update the disk block and the denode, and then write the disk block
 * out to disk.	 This way disk blocks containing directory entries and in
 * memory denode's will be in synch.
 */
static int
msdosfs_findslot(struct denode *dp, struct componentname *cnp)
{
	daddr_t bn;
	int error;
	int slotcount;
	int slotoffset = 0;
	int frcn;
	u_long cluster;
	int blkoff;
	u_int diroff;
	int blsize;
	struct msdosfsmount *pmp;
	struct buf *bp = 0;
	struct direntry *dep;
	u_char dosfilename[12];
	int wincnt = 1;
	int chksum = -1, chksum_ok;
	int olddos = 1;

	pmp = dp->de_pmp;

	switch (unix2dosfn((const u_char *)cnp->cn_nameptr, dosfilename,
	    cnp->cn_namelen, 0)) {
	case 0:
		return (EINVAL);
	case 1:
		break;
	case 2:
		wincnt = winSlotCnt((const u_char *)cnp->cn_nameptr,
		    cnp->cn_namelen, pmp->pm_flags & MSDOSFSMNT_UTF8) + 1;
		break;
	case 3:
		olddos = 0;
		wincnt = winSlotCnt((const u_char *)cnp->cn_nameptr,
		    cnp->cn_namelen, pmp->pm_flags & MSDOSFSMNT_UTF8) + 1;
		break;
	}

	if (pmp->pm_flags & MSDOSFSMNT_SHORTNAME)
		wincnt = 1;

	/*
	 * Suppress search for slots unless creating
	 * file and at end of pathname, in which case
	 * we watch for a place to put the new file in
	 * case it doesn't already exist.
	 */
	slotcount = 0;
	DPRINTF(("%s(): dos filename: %s\n", __func__, dosfilename));
	/*
	 * Search the directory pointed at by vdp for the name pointed at
	 * by cnp->cn_nameptr.
	 */
	/*
	 * The outer loop ranges over the clusters that make up the
	 * directory.  Note that the root directory is different from all
	 * other directories.  It has a fixed number of blocks that are not
	 * part of the pool of allocatable clusters.  So, we treat it a
	 * little differently. The root directory starts at "cluster" 0.
	 */
	diroff = 0;
	for (frcn = 0; diroff < dp->de_FileSize; frcn++) {
		if ((error = pcbmap(dp, frcn, &bn, &cluster, &blsize)) != 0) {
			if (error == E2BIG)
				break;
			return (error);
		}
		error = bread(pmp->pm_devvp, de_bn2kb(pmp, bn), blsize,
		    0, &bp);
		if (error) {
			return (error);
		}
		for (blkoff = 0; blkoff < blsize;
		     blkoff += sizeof(struct direntry),
		     diroff += sizeof(struct direntry)) {
			dep = (struct direntry *)((char *)bp->b_data + blkoff);
			/*
			 * If the slot is empty and we are still looking
			 * for an empty then remember this one.	 If the
			 * slot is not empty then check to see if it
			 * matches what we are looking for.  If the slot
			 * has never been filled with anything, then the
			 * remainder of the directory has never been used,
			 * so there is no point in searching it.
			 */
			if (dep->deName[0] == SLOT_EMPTY ||
			    dep->deName[0] == SLOT_DELETED) {
				/*
				 * Drop memory of previous long matches
				 */
				chksum = -1;

				if (slotcount < wincnt) {
					slotcount++;
					slotoffset = diroff;
				}
				if (dep->deName[0] == SLOT_EMPTY) {
					brelse(bp);
					goto notfound;
				}
			} else {
				/*
				 * If there wasn't enough space for our
				 * winentries, forget about the empty space
				 */
				if (slotcount < wincnt)
					slotcount = 0;

				/*
				 * Check for Win95 long filename entry
				 */
				if (dep->deAttributes == ATTR_WIN95) {
					if (pmp->pm_flags & MSDOSFSMNT_SHORTNAME)
						continue;

					chksum = winChkName((const u_char *)cnp->cn_nameptr,
							    cnp->cn_namelen,
							    (struct winentry *)dep,
							    chksum,
							    pmp->pm_flags & MSDOSFSMNT_UTF8);
					continue;
				}

				/*
				 * Ignore volume labels (anywhere, not just
				 * the root directory).
				 */
				if (dep->deAttributes & ATTR_VOLUME) {
					chksum = -1;
					continue;
				}

				/*
				 * Check for a checksum or name match
				 */
				chksum_ok = (chksum == winChksum(dep->deName));
				if (!chksum_ok
				    && (!olddos || memcmp(dosfilename, dep->deName, 11))) {
					chksum = -1;
					continue;
				}
				DPRINTF(("%s(): match blkoff %d, diroff %d\n",
				    __func__, blkoff, diroff));
				/*
				 * Remember where this directory
				 * entry came from for whoever did
				 * this lookup.
				 */
				dp->de_fndoffset = diroff;
				dp->de_fndcnt = 0;

				return EEXIST;
			}
		}	/* for (blkoff = 0; .... */
		/*
		 * Release the buffer holding the directory cluster just
		 * searched.
		 */
		brelse(bp);
	}	/* for (frcn = 0; ; frcn++) */

notfound:
	/*
	 * We hold no disk buffers at this point.
	 */

	/*
	 * If we get here we didn't find the entry we were looking for. But
	 * that's ok if we are creating or renaming and are at the end of
	 * the pathname and the directory hasn't been removed.
	 */
	DPRINTF(("%s(): refcnt %ld, slotcount %d, slotoffset %d\n",
	    __func__, dp->de_refcnt, slotcount, slotoffset));
	/*
	 * Fixup the slot description to point to the place where
	 * we might put the new DOS direntry (putting the Win95
	 * long name entries before that)
	 */
	if (!slotcount) {
		slotcount = 1;
		slotoffset = diroff;
	}
	if (wincnt > slotcount) {
		slotoffset += sizeof(struct direntry) * (wincnt - slotcount);
	}

	/*
	 * Return an indication of where the new directory
	 * entry should be put.
	 */
	dp->de_fndoffset = slotoffset;
	dp->de_fndcnt = wincnt - 1;

	/*
	 * We return with the directory locked, so that
	 * the parameters we set up above will still be
	 * valid if we actually decide to do a direnter().
	 * We return ni_vp == NULL to indicate that the entry
	 * does not currently exist; we leave a pointer to
	 * the (locked) directory inode in ndp->ni_dvp.
	 *
	 * NB - if the directory is unlocked, then this
	 * information cannot be used.
	 */
	return 0;
}

/*
 * Create a regular file. On entry the directory to contain the file being
 * created is locked.  We must release before we return.
 */
struct denode *
msdosfs_mkfile(const char *path, struct denode *pdep, fsnode *node)
{
	struct componentname cn;
	struct denode ndirent;
	struct denode *dep;
	int error;
	struct stat *st = &node->inode->st;
	struct msdosfsmount *pmp = pdep->de_pmp;

	cn.cn_nameptr = node->name;
	cn.cn_namelen = strlen(node->name);

	DPRINTF(("%s(name %s, mode 0%o size %zu)\n", __func__, node->name,
	    st->st_mode, (size_t)st->st_size));

	/*
	 * If this is the root directory and there is no space left we
	 * can't do anything.  This is because the root directory can not
	 * change size.
	 */
	if (pdep->de_StartCluster == MSDOSFSROOT
	    && pdep->de_fndoffset >= pdep->de_FileSize) {
		error = ENOSPC;
		goto bad;
	}

	/*
	 * Create a directory entry for the file, then call createde() to
	 * have it installed. NOTE: DOS files are always executable.  We
	 * use the absence of the owner write bit to make the file
	 * readonly.
	 */
	memset(&ndirent, 0, sizeof(ndirent));
	if ((error = uniqdosname(pdep, &cn, ndirent.de_Name)) != 0)
		goto bad;

	ndirent.de_Attributes = (st->st_mode & S_IWUSR) ?
				ATTR_ARCHIVE : ATTR_ARCHIVE | ATTR_READONLY;
	ndirent.de_StartCluster = 0;
	ndirent.de_FileSize = 0;
	ndirent.de_dev = pdep->de_dev;
	ndirent.de_devvp = pdep->de_devvp;
	ndirent.de_pmp = pdep->de_pmp;
	ndirent.de_flag = DE_ACCESS | DE_CREATE | DE_UPDATE;
	msdosfs_times(pmp, &ndirent, st);
	if ((error = msdosfs_findslot(pdep, &cn)) != 0)
		goto bad;
	if ((error = createde(&ndirent, pdep, &dep, &cn)) != 0)
		goto bad;
	if ((error = msdosfs_wfile(path, dep, node)) != 0)
		goto bad;
	return dep;

bad:
	errno = error;
	return NULL;
}
static int
msdosfs_updatede(struct denode *dep)
{
	struct buf *bp;
	struct direntry *dirp;
	int error;

	dep->de_flag &= ~DE_MODIFIED;
	error = readde(dep, &bp, &dirp);
	if (error)
		return error;
	DE_EXTERNALIZE(dirp, dep);
	error = bwrite(bp);
	return error;
}

/*
 * Write data to a file or directory.
 */
static int
msdosfs_wfile(const char *path, struct denode *dep, fsnode *node)
{
	int error, fd;
	size_t osize = dep->de_FileSize;
	struct stat *st = &node->inode->st;
	size_t nsize, offs;
	struct msdosfsmount *pmp = dep->de_pmp;
	struct buf *bp;
	char *dat;
	u_long cn = 0;

	error = 0;	/* XXX: gcc/vax */
	DPRINTF(("%s(diroff %lu, dirclust %lu, startcluster %lu)\n", __func__,
	    dep->de_diroffset, dep->de_dirclust, dep->de_StartCluster));
	if (st->st_size == 0)
		return 0;

	/* Don't bother to try to write files larger than the fs limit */
	if (st->st_size > MSDOSFS_FILESIZE_MAX)
		return EFBIG;

	nsize = st->st_size;
	DPRINTF(("%s(nsize=%zu, osize=%zu)\n", __func__, nsize, osize));
	if (nsize > osize) {
		if ((error = deextend(dep, nsize, NULL)) != 0)
			return error;
		if ((error = msdosfs_updatede(dep)) != 0)
			return error;
	}

	if ((fd = open(path, O_RDONLY)) == -1) {
		error = errno;
		DPRINTF((1, "open %s: %s", path, strerror(error)));
		return error;
	}

	if ((dat = mmap(0, nsize, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0))
	    == MAP_FAILED) {
		error = errno;
		DPRINTF(("%s: mmap %s: %s", __func__, node->name,
		    strerror(error)));
		close(fd);
		goto out;
	}
	close(fd);

	for (offs = 0; offs < nsize;) {
		int blsize, cpsize;
		daddr_t bn;
		u_long on = offs & pmp->pm_crbomask;
#ifdef HACK
		cn = dep->de_StartCluster;
		if (cn == MSDOSFSROOT) {
			DPRINTF(("%s: bad lbn %lu", __func__, cn));
			error = EINVAL;
			goto out;
		}
		bn = cntobn(pmp, cn);
		blsize = pmp->pm_bpcluster;
#else
		if ((error = pcbmap(dep, cn++, &bn, NULL, &blsize)) != 0) {
			DPRINTF(("%s: pcbmap %lu", __func__, bn));
			goto out;
		}
#endif
		DPRINTF(("%s(cn=%lu, bn=%llu/%llu, blsize=%d)\n", __func__,
		    cn, (unsigned long long)bn,
		    (unsigned long long)de_bn2kb(pmp, bn), blsize));
		if ((error = bread(pmp->pm_devvp, de_bn2kb(pmp, bn), blsize,
		    0, &bp)) != 0) {
			DPRINTF(("bread %d\n", error));
			goto out;
		}
		cpsize = MIN((nsize - offs), blsize - on);
		memcpy((char *)bp->b_data + on, dat + offs, cpsize);
		bwrite(bp);
		offs += cpsize;
	}

	munmap(dat, nsize);
	return 0;
out:
	munmap(dat, nsize);
	return error;
}


static const struct {
	struct direntry dot;
	struct direntry dotdot;
} dosdirtemplate = {
	{	".       ", "   ",			/* the . entry */
		ATTR_DIRECTORY,				/* file attribute */
		0,					/* reserved */
		0, { 0, 0 }, { 0, 0 },			/* create time & date */
		{ 0, 0 },				/* access date */
		{ 0, 0 },				/* high bits of start cluster */
		{ 210, 4 }, { 210, 4 },			/* modify time & date */
		{ 0, 0 },				/* startcluster */
		{ 0, 0, 0, 0 }				/* filesize */
	},
	{	"..      ", "   ",			/* the .. entry */
		ATTR_DIRECTORY,				/* file attribute */
		0,					/* reserved */
		0, { 0, 0 }, { 0, 0 },			/* create time & date */
		{ 0, 0 },				/* access date */
		{ 0, 0 },				/* high bits of start cluster */
		{ 210, 4 }, { 210, 4 },			/* modify time & date */
		{ 0, 0 },				/* startcluster */
		{ 0, 0, 0, 0 }				/* filesize */
	}
};

struct denode *
msdosfs_mkdire(const char *path, struct denode *pdep, fsnode *node) {
	struct denode ndirent;
	struct denode *dep;
	struct componentname cn;
	struct stat *st = &node->inode->st;
	struct msdosfsmount *pmp = pdep->de_pmp;
	int error;
	u_long newcluster, pcl, bn;
	daddr_t lbn;
	struct direntry *denp;
	struct buf *bp;

	cn.cn_nameptr = node->name;
	cn.cn_namelen = strlen(node->name);
	/*
	 * If this is the root directory and there is no space left we
	 * can't do anything.  This is because the root directory can not
	 * change size.
	 */
	if (pdep->de_StartCluster == MSDOSFSROOT
	    && pdep->de_fndoffset >= pdep->de_FileSize) {
		error = ENOSPC;
		goto bad2;
	}

	/*
	 * Allocate a cluster to hold the about to be created directory.
	 */
	error = clusteralloc(pmp, 0, 1, &newcluster, NULL);
	if (error)
		goto bad2;

	memset(&ndirent, 0, sizeof(ndirent));
	ndirent.de_pmp = pmp;
	ndirent.de_flag = DE_ACCESS | DE_CREATE | DE_UPDATE;
	msdosfs_times(pmp, &ndirent, st);

	/*
	 * Now fill the cluster with the "." and ".." entries. And write
	 * the cluster to disk.	 This way it is there for the parent
	 * directory to be pointing at if there were a crash.
	 */
	bn = cntobn(pmp, newcluster);
	lbn = de_bn2kb(pmp, bn);
	DPRINTF(("%s(newcluster %lu, bn=%lu, lbn=%lu)\n", __func__, newcluster,
	    bn, lbn));
	/* always succeeds */
	bp = getblk(pmp->pm_devvp, lbn, pmp->pm_bpcluster, 0, 0);
	memset(bp->b_data, 0, pmp->pm_bpcluster);
	memcpy(bp->b_data, &dosdirtemplate, sizeof dosdirtemplate);
	denp = (struct direntry *)bp->b_data;
	putushort(denp[0].deStartCluster, newcluster);
	putushort(denp[0].deCDate, ndirent.de_CDate);
	putushort(denp[0].deCTime, ndirent.de_CTime);
	denp[0].deCHundredth = ndirent.de_CHun;
	putushort(denp[0].deADate, ndirent.de_ADate);
	putushort(denp[0].deMDate, ndirent.de_MDate);
	putushort(denp[0].deMTime, ndirent.de_MTime);
	pcl = pdep->de_StartCluster;
	DPRINTF(("%s(pcl %lu, rootdirblk=%lu)\n", __func__, pcl,
	    pmp->pm_rootdirblk));
	if (FAT32(pmp) && pcl == pmp->pm_rootdirblk)
		pcl = 0;
	putushort(denp[1].deStartCluster, pcl);
	putushort(denp[1].deCDate, ndirent.de_CDate);
	putushort(denp[1].deCTime, ndirent.de_CTime);
	denp[1].deCHundredth = ndirent.de_CHun;
	putushort(denp[1].deADate, ndirent.de_ADate);
	putushort(denp[1].deMDate, ndirent.de_MDate);
	putushort(denp[1].deMTime, ndirent.de_MTime);
	if (FAT32(pmp)) {
		putushort(denp[0].deHighClust, newcluster >> 16);
		putushort(denp[1].deHighClust, pdep->de_StartCluster >> 16);
	} else {
		putushort(denp[0].deHighClust, 0);
		putushort(denp[1].deHighClust, 0);
	}

	if ((error = bwrite(bp)) != 0)
		goto bad;

	/*
	 * Now build up a directory entry pointing to the newly allocated
	 * cluster.  This will be written to an empty slot in the parent
	 * directory.
	 */
	if ((error = uniqdosname(pdep, &cn, ndirent.de_Name)) != 0)
		goto bad;

	ndirent.de_Attributes = ATTR_DIRECTORY;
	ndirent.de_StartCluster = newcluster;
	ndirent.de_FileSize = 0;
	ndirent.de_dev = pdep->de_dev;
	ndirent.de_devvp = pdep->de_devvp;
	ndirent.de_pmp = pdep->de_pmp;
	if ((error = msdosfs_findslot(pdep, &cn)) != 0)
		goto bad;
	if ((error = createde(&ndirent, pdep, &dep, &cn)) != 0)
		goto bad;
	if ((error = msdosfs_updatede(dep)) != 0)
		goto bad;
	return dep;

bad:
	clusterfree(pmp, newcluster, NULL);
bad2:
	errno = error;
	return NULL;
}
