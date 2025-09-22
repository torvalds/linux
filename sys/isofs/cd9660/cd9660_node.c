/*	$OpenBSD: cd9660_node.c,v 1.38 2021/10/19 06:11:45 semarie Exp $	*/
/*	$NetBSD: cd9660_node.c,v 1.17 1997/05/05 07:13:57 mycroft Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1989, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)cd9660_node.c	8.5 (Berkeley) 12/5/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/stat.h>

#include <crypto/siphash.h>

#include <isofs/cd9660/iso.h>
#include <isofs/cd9660/cd9660_extern.h>
#include <isofs/cd9660/cd9660_node.h>

/*
 * Structures associated with iso_node caching.
 */
u_int cd9660_isohash(dev_t, cdino_t);

struct iso_node **isohashtbl;
u_long isohash;
SIPHASH_KEY isohashkey;
#define	INOHASH(device, inum) cd9660_isohash((device), (inum))

extern int prtactive;	/* 1 => print out reclaim of active vnodes */

static u_int cd9660_chars2ui(u_char *, int);

/*
 * Initialize hash links for inodes and dnodes.
 */
int
cd9660_init(struct vfsconf *vfsp)
{

	isohashtbl = hashinit(initialvnodes, M_ISOFSMNT, M_WAITOK, &isohash);
	arc4random_buf(&isohashkey, sizeof(isohashkey));
	return (0);
}

u_int
cd9660_isohash(dev_t device, cdino_t inum)
{
	SIPHASH_CTX ctx;

	SipHash24_Init(&ctx, &isohashkey);
	SipHash24_Update(&ctx, &device, sizeof(device));
	SipHash24_Update(&ctx, &inum, sizeof(inum));
	return (SipHash24_End(&ctx) & isohash);
}

/*
 * Use the device/inum pair to find the incore inode, and return a pointer
 * to it. If it is in core, but locked, wait for it.
 */
struct vnode *
cd9660_ihashget(dev_t dev, cdino_t inum)
{
	struct iso_node *ip;
	struct vnode *vp;

loop:
	/* XXX locking lock hash list? */
       for (ip = isohashtbl[INOHASH(dev, inum)]; ip; ip = ip->i_next) {
               if (inum == ip->i_number && dev == ip->i_dev) {
                       vp = ITOV(ip);
			/* XXX locking unlock hash list? */
                       if (vget(vp, LK_EXCLUSIVE))
                               goto loop;
                       return (vp);
	       }
       }
	/* XXX locking unlock hash list? */
       return (NULL);
}

/*
 * Insert the inode into the hash table, and return it locked.
 */
int
cd9660_ihashins(struct iso_node *ip)
{
	struct iso_node **ipp, *iq;

	/* XXX locking lock hash list? */
	ipp = &isohashtbl[INOHASH(ip->i_dev, ip->i_number)];

	for (iq = *ipp; iq; iq = iq->i_next) {
		if (iq->i_dev == ip->i_dev &&
		    iq->i_number == ip->i_number)
			return (EEXIST);
	}
			      
	if ((iq = *ipp) != NULL)
		iq->i_prev = &ip->i_next;
	ip->i_next = iq;
	ip->i_prev = ipp;
	*ipp = ip;
	/* XXX locking unlock hash list? */

	VOP_LOCK(ITOV(ip), LK_EXCLUSIVE);

	return (0);
}

/*
 * Remove the inode from the hash table.
 */
void
cd9660_ihashrem(struct iso_node *ip)
{
	struct iso_node *iq;

	if (ip->i_prev == NULL)
		return;
	
	/* XXX locking lock hash list? */
	if ((iq = ip->i_next) != NULL)
		iq->i_prev = ip->i_prev;
	*ip->i_prev = iq;
#ifdef DIAGNOSTIC
	ip->i_next = NULL;
	ip->i_prev = NULL;
#endif
	/* XXX locking unlock hash list? */
}

/*
 * Last reference to an inode, write the inode out and if necessary,
 * truncate and deallocate the file.
 */
int
cd9660_inactive(void *v)
{
	struct vop_inactive_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct iso_node *ip = VTOI(vp);
	int error = 0;

#ifdef DIAGNOSTIC
	if (prtactive && vp->v_usecount != 0)
		vprint("cd9660_inactive: pushing active", vp);
#endif
	
	ip->i_flag = 0;
	VOP_UNLOCK(vp);
	/*
	 * If we are done with the inode, reclaim it
	 * so that it can be reused immediately.
	 */
	if (ip->inode.iso_mode == 0)
		vrecycle(vp, ap->a_p);

	return (error);
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 */
int
cd9660_reclaim(void *v)
{
	struct vop_reclaim_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct iso_node *ip = VTOI(vp);

#ifdef DIAGNOSTIC
	if (prtactive && vp->v_usecount != 0)
		vprint("cd9660_reclaim: pushing active", vp);
#endif

	/*
	 * Remove the inode from its hash chain.
	 */
	cd9660_ihashrem(ip);
	/*
	 * Purge old data structures associated with the inode.
	 */
	cache_purge(vp);
	if (ip->i_devvp) {
		vrele(ip->i_devvp);
		ip->i_devvp = 0;
	}
	free(vp->v_data, M_ISOFSNODE, 0);
	vp->v_data = NULL;
	return (0);
}

/*
 * File attributes
 */
void
cd9660_defattr(struct iso_directory_record *isodir, struct iso_node *inop,
    struct buf *bp)
{
	struct buf *bp2 = NULL;
	struct iso_mnt *imp;
	struct iso_extended_attributes *ap = NULL;
	int off;
	
	if (isonum_711(isodir->flags)&2) {
		inop->inode.iso_mode = S_IFDIR;
		/*
		 * If we return 2, fts() will assume there are no subdirectories
		 * (just links for the path and .), so instead we return 1.
		 */
		inop->inode.iso_links = 1;
	} else {
		inop->inode.iso_mode = S_IFREG;
		inop->inode.iso_links = 1;
	}
	if (!bp
	    && ((imp = inop->i_mnt)->im_flags & ISOFSMNT_EXTATT)
	    && (off = isonum_711(isodir->ext_attr_length))) {
		cd9660_bufatoff(inop, (off_t)-(off << imp->im_bshift), NULL,
			     &bp2);
		bp = bp2;
	}
	if (bp) {
		ap = (struct iso_extended_attributes *)bp->b_data;
		
		if (isonum_711(ap->version) == 1) {
			if (!(ap->perm[1]&0x10))
				inop->inode.iso_mode |= S_IRUSR;
			if (!(ap->perm[1]&0x40))
				inop->inode.iso_mode |= S_IXUSR;
			if (!(ap->perm[0]&0x01))
				inop->inode.iso_mode |= S_IRGRP;
			if (!(ap->perm[0]&0x04))
				inop->inode.iso_mode |= S_IXGRP;
			if (!(ap->perm[0]&0x10))
				inop->inode.iso_mode |= S_IROTH;
			if (!(ap->perm[0]&0x40))
				inop->inode.iso_mode |= S_IXOTH;
			inop->inode.iso_uid = isonum_723(ap->owner); /* what about 0? */
			inop->inode.iso_gid = isonum_723(ap->group); /* what about 0? */
		} else
			ap = NULL;
	}
	if (!ap) {
		inop->inode.iso_mode |=
		    S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
		inop->inode.iso_uid = (uid_t)0;
		inop->inode.iso_gid = (gid_t)0;
	}
	if (bp2)
		brelse(bp2);
}

/*
 * Time stamps
 */
void
cd9660_deftstamp(struct iso_directory_record *isodir, struct iso_node *inop,
    struct buf *bp)
{
	struct buf *bp2 = NULL;
	struct iso_mnt *imp;
	struct iso_extended_attributes *ap = NULL;
	int off;
	
	if (!bp
	    && ((imp = inop->i_mnt)->im_flags & ISOFSMNT_EXTATT)
	    && (off = isonum_711(isodir->ext_attr_length))) {
		cd9660_bufatoff(inop, (off_t)-(off << imp->im_bshift), NULL,
			     &bp2);
		bp = bp2;
	}
	if (bp) {
		ap = (struct iso_extended_attributes *)bp->b_data;
		
		if (isonum_711(ap->version) == 1) {
			if (!cd9660_tstamp_conv17(ap->ftime,&inop->inode.iso_atime))
				cd9660_tstamp_conv17(ap->ctime,&inop->inode.iso_atime);
			if (!cd9660_tstamp_conv17(ap->ctime,&inop->inode.iso_ctime))
				inop->inode.iso_ctime = inop->inode.iso_atime;
			if (!cd9660_tstamp_conv17(ap->mtime,&inop->inode.iso_mtime))
				inop->inode.iso_mtime = inop->inode.iso_ctime;
		} else
			ap = NULL;
	}
	if (!ap) {
		cd9660_tstamp_conv7(isodir->date,&inop->inode.iso_ctime);
		inop->inode.iso_atime = inop->inode.iso_ctime;
		inop->inode.iso_mtime = inop->inode.iso_ctime;
	}
	if (bp2)
		brelse(bp2);
}

int
cd9660_tstamp_conv7(u_char *pi, struct timespec *pu)
{
	int crtime, days;
	int y, m, d, hour, minute, second;
	signed char tz;
	
	y = pi[0] + 1900;
	m = pi[1];
	d = pi[2];
	hour = pi[3];
	minute = pi[4];
	second = pi[5];
	tz = (signed char) pi[6];
	
	if (y < 1970) {
		pu->tv_sec  = 0;
		pu->tv_nsec = 0;
		return (0);
	} else {
#ifdef	ORIGINAL
		/* computes day number relative to Sept. 19th,1989 */
		/* don't even *THINK* about changing formula. It works! */
		days = 367*(y-1980)-7*(y+(m+9)/12)/4-3*((y+(m-9)/7)/100+1)/4+275*m/9+d-100;
#else
		/*
		 * Changed :-) to make it relative to Jan. 1st, 1970
		 * and to disambiguate negative division
		 */
		days = 367*(y-1960)-7*(y+(m+9)/12)/4-3*((y+(m+9)/12-1)/100+1)/4+275*m/9+d-239;
#endif
		crtime = ((((days * 24) + hour) * 60 + minute) * 60) + second;
		
		/* timezone offset is unreliable on some disks */
		if (-48 <= tz && tz <= 52)
			crtime -= tz * 15 * 60;
	}
	pu->tv_sec  = crtime;
	pu->tv_nsec = 0;
	return (1);
}

static u_int
cd9660_chars2ui(u_char *begin, int len)
{
	u_int rc;
	
	for (rc = 0; --len >= 0;) {
		rc *= 10;
		rc += *begin++ - '0';
	}
	return (rc);
}

int
cd9660_tstamp_conv17(u_char *pi, struct timespec *pu)
{
	u_char buf[7];
	
	/* year:"0001"-"9999" -> -1900  */
	buf[0] = cd9660_chars2ui(pi,4) - 1900;
	
	/* month: " 1"-"12"      -> 1 - 12 */
	buf[1] = cd9660_chars2ui(pi + 4,2);
	
	/* day:   " 1"-"31"      -> 1 - 31 */
	buf[2] = cd9660_chars2ui(pi + 6,2);
	
	/* hour:  " 0"-"23"      -> 0 - 23 */
	buf[3] = cd9660_chars2ui(pi + 8,2);
	
	/* minute:" 0"-"59"      -> 0 - 59 */
	buf[4] = cd9660_chars2ui(pi + 10,2);
	
	/* second:" 0"-"59"      -> 0 - 59 */
	buf[5] = cd9660_chars2ui(pi + 12,2);
	
	/* difference of GMT */
	buf[6] = pi[16];
	
	return (cd9660_tstamp_conv7(buf,pu));
}

cdino_t
isodirino(struct iso_directory_record *isodir, struct iso_mnt *imp)
{
	cdino_t ino;

	ino = (isonum_733(isodir->extent) +
	    isonum_711(isodir->ext_attr_length)) << imp->im_bshift;
	return (ino);
}
