/*	$OpenBSD: ntfs_vnops.c,v 1.51 2024/10/18 05:52:32 miod Exp $	*/
/*	$NetBSD: ntfs_vnops.c,v 1.6 2003/04/10 21:57:26 jdolecek Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * John Heidemann of the UCLA Ficus project.
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
 *	Id: ntfs_vnops.c,v 1.5 1999/05/12 09:43:06 semenu Exp
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/dirent.h>
#include <sys/specdev.h>

/*#define NTFS_DEBUG 1*/
#include <ntfs/ntfs.h>
#include <ntfs/ntfs_inode.h>
#include <ntfs/ntfs_subr.h>

#include <sys/unistd.h> /* for pathconf(2) constants */

int	ntfs_read(void *);
int	ntfs_getattr(void *);
int	ntfs_inactive(void *);
int	ntfs_print(void *);
int	ntfs_reclaim(void *);
int	ntfs_strategy(void *);
int	ntfs_access(void *v);
int	ntfs_open(void *v);
int	ntfs_close(void *);
int	ntfs_readdir(void *);
int	ntfs_lookup(void *);
int	ntfs_bmap(void *);
int	ntfs_fsync(void *);
int	ntfs_pathconf(void *);

int	ntfs_prtactive = 0;	/* 1 => print out reclaim of active vnodes */

/*
 * This is a noop, simply returning what one has been given.
 */
int
ntfs_bmap(void *v)
{
	struct vop_bmap_args *ap = v;
	DPRINTF("ntfs_bmap: vn: %p, blk: %lld\n",
	    ap->a_vp, (long long)ap->a_bn);
	if (ap->a_vpp != NULL)
		*ap->a_vpp = ap->a_vp;
	if (ap->a_bnp != NULL)
		*ap->a_bnp = ap->a_bn;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	return (0);
}

int
ntfs_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct uio *uio = ap->a_uio;
	struct ntfsmount *ntmp = ip->i_mp;
	u_int64_t toread;
	int error;

	DPRINTF("ntfs_read: ino: %u, off: %lld resid: %zu, segflg: %d\n",
	    ip->i_number, uio->uio_offset, uio->uio_resid, uio->uio_segflg);

	DPRINTF("ntfs_read: filesize: %llu", fp->f_size);

	/* don't allow reading after end of file */
	if (uio->uio_offset > fp->f_size)
		toread = 0;
	else
		toread = MIN(uio->uio_resid, fp->f_size - uio->uio_offset);

	DPRINTF(", toread: %llu\n", toread);

	if (toread == 0)
		return (0);

	error = ntfs_readattr(ntmp, ip, fp->f_attrtype,
		fp->f_attrname, uio->uio_offset, toread, NULL, uio);
	if (error) {
		printf("ntfs_read: ntfs_readattr failed: %d\n",error);
		return (error);
	}

	return (0);
}

int
ntfs_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct vattr *vap = ap->a_vap;

	DPRINTF("ntfs_getattr: %u, flags: %u\n", ip->i_number, ip->i_flag);

	vap->va_fsid = ip->i_dev;
	vap->va_fileid = ip->i_number;
	vap->va_mode = ip->i_mp->ntm_mode;
	vap->va_nlink = ip->i_nlink;
	vap->va_uid = ip->i_mp->ntm_uid;
	vap->va_gid = ip->i_mp->ntm_gid;
	vap->va_rdev = 0;				/* XXX UNODEV ? */
	vap->va_size = fp->f_size;
	vap->va_bytes = fp->f_allocated;
	vap->va_atime = ntfs_nttimetounix(fp->f_times.t_access);
	vap->va_mtime = ntfs_nttimetounix(fp->f_times.t_write);
	vap->va_ctime = ntfs_nttimetounix(fp->f_times.t_create);
	vap->va_flags = ip->i_flag;
	vap->va_gen = 0;
	vap->va_blocksize = ip->i_mp->ntm_spc * ip->i_mp->ntm_bps;
	vap->va_type = vp->v_type;
	vap->va_filerev = 0;

	/*
	 * Ensure that a directory link count is always 1 so that things
	 * like fts_read() do not try to be smart and end up skipping over
	 * directories. Additionally, ip->i_nlink will not be initialised
	 * until the ntnode has been loaded for the file.
	 */
	if (vp->v_type == VDIR || ip->i_nlink < 1)
		vap->va_nlink = 1;

	return (0);
}


/*
 * Last reference to an ntnode.  If necessary, write or delete it.
 */
int
ntfs_inactive(void *v)
{
	struct vop_inactive_args *ap = v;
	struct vnode *vp = ap->a_vp;
#ifdef NTFS_DEBUG
	struct ntnode *ip = VTONT(vp);
#endif

	DPRINTF("ntfs_inactive: vnode: %p, ntnode: %u\n", vp, ip->i_number);

#ifdef DIAGNOSTIC
	if (ntfs_prtactive && vp->v_usecount != 0)
		vprint("ntfs_inactive: pushing active", vp);
#endif

	VOP_UNLOCK(vp);

	/* XXX since we don't support any filesystem changes
	 * right now, nothing more needs to be done
	 */
	return (0);
}

/*
 * Reclaim an fnode/ntnode so that it can be used for other purposes.
 */
int
ntfs_reclaim(void *v)
{
	struct vop_reclaim_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	int error;

	DPRINTF("ntfs_reclaim: vnode: %p, ntnode: %u\n", vp, ip->i_number);

#ifdef DIAGNOSTIC
	if (ntfs_prtactive && vp->v_usecount != 0)
		vprint("ntfs_reclaim: pushing active", vp);
#endif

	if ((error = ntfs_ntget(ip)) != 0)
		return (error);
	
	/* Purge old data structures associated with the inode. */
	cache_purge(vp);

	ntfs_frele(fp);
	ntfs_ntput(ip);

	vp->v_data = NULL;

	return (0);
}

int
ntfs_print(void *v)
{
#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
	struct vop_print_args *ap = v;
	struct ntnode *ip = VTONT(ap->a_vp);

	printf("tag VT_NTFS, ino %u, flag %#x, usecount %d, nlink %ld\n",
	    ip->i_number, ip->i_flag, ip->i_usecount, ip->i_nlink);
#endif

	return (0);
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 */
int
ntfs_strategy(void *v)
{
	struct vop_strategy_args *ap = v;
	struct buf *bp = ap->a_bp;
	struct vnode *vp = bp->b_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct ntfsmount *ntmp = ip->i_mp;
	int error, s;

	DPRINTF("ntfs_strategy: blkno: %lld, lblkno: %lld\n",
	    (long long)bp->b_blkno, (long long)bp->b_lblkno);

	DPRINTF("strategy: bcount: %ld flags: 0x%lx\n",
	    bp->b_bcount, bp->b_flags);

	if (bp->b_flags & B_READ) {
		u_int32_t toread;

		if (ntfs_cntob(bp->b_blkno) >= fp->f_size) {
			clrbuf(bp);
			error = 0;
		} else {
			toread = MIN(bp->b_bcount,
				 fp->f_size - ntfs_cntob(bp->b_blkno));
			DPRINTF("ntfs_strategy: toread: %u, fsize: %llu\n",
			    toread, fp->f_size);

			error = ntfs_readattr(ntmp, ip, fp->f_attrtype,
				fp->f_attrname, ntfs_cntob(bp->b_blkno),
				toread, bp->b_data, NULL);

			if (error) {
				printf("ntfs_strategy: ntfs_readattr failed\n");
				bp->b_error = error;
				bp->b_flags |= B_ERROR;
			}

			bzero(bp->b_data + toread, bp->b_bcount - toread);
		}
	} else {
		bp->b_error = error = EROFS;
		bp->b_flags |= B_ERROR;
	}
	s = splbio();
	biodone(bp);
	splx(s);
	return (error);
}

int
ntfs_access(void *v)
{
	struct vop_access_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct ntnode *ip = VTONT(vp);
	struct ucred *cred = ap->a_cred;
	mode_t mask, mode = ap->a_mode;
	gid_t *gp;
	int i;

	DPRINTF("ntfs_access: %u\n", ip->i_number);

	/*
	 * Disallow write attempts unless the file is a socket, fifo, or
	 * a block or character device resident on the file system.
	 */
	if (mode & VWRITE) {
		switch ((int)vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			return (EROFS);
		}
	}

	/* Otherwise, user id 0 always gets access. */
	if (cred->cr_uid == 0)
		return (0);

	mask = 0;

	/* Otherwise, check the owner. */
	if (cred->cr_uid == ip->i_mp->ntm_uid) {
		if (mode & VEXEC)
			mask |= S_IXUSR;
		if (mode & VREAD)
			mask |= S_IRUSR;
		if (mode & VWRITE)
			mask |= S_IWUSR;
		return ((ip->i_mp->ntm_mode & mask) == mask ? 0 : EACCES);
	}

	/* Otherwise, check the groups. */
	for (i = 0, gp = cred->cr_groups; i < cred->cr_ngroups; i++, gp++)
		if (ip->i_mp->ntm_gid == *gp) {
			if (mode & VEXEC)
				mask |= S_IXGRP;
			if (mode & VREAD)
				mask |= S_IRGRP;
			if (mode & VWRITE)
				mask |= S_IWGRP;
			return ((ip->i_mp->ntm_mode&mask) == mask ? 0 : EACCES);
		}

	/* Otherwise, check everyone else. */
	if (mode & VEXEC)
		mask |= S_IXOTH;
	if (mode & VREAD)
		mask |= S_IROTH;
	if (mode & VWRITE)
		mask |= S_IWOTH;
	return ((ip->i_mp->ntm_mode & mask) == mask ? 0 : EACCES);
}

/*
 * Open called.
 *
 * Nothing to do.
 */
int
ntfs_open(void *v)
{
#if NTFS_DEBUG
	struct vop_open_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct ntnode *ip = VTONT(vp);

	printf("ntfs_open: %d\n",ip->i_number);
#endif

	/*
	 * Files marked append-only must be opened for appending.
	 */

	return (0);
}

/*
 * Close called.
 *
 * Update the times on the inode.
 */
int
ntfs_close(void *v)
{
#if NTFS_DEBUG
	struct vop_close_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct ntnode *ip = VTONT(vp);

	printf("ntfs_close: %d\n",ip->i_number);
#endif

	return (0);
}

int
ntfs_readdir(void *v)
{
	struct vop_readdir_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct uio *uio = ap->a_uio;
	struct ntfsmount *ntmp = ip->i_mp;
	int i, error = 0;
	u_int32_t faked = 0, num;
	struct dirent cde;
	off_t off;

	DPRINTF("ntfs_readdir %u off: %lld resid: %zu\n", ip->i_number,
	    uio->uio_offset, uio->uio_resid);

	off = uio->uio_offset;
	memset(&cde, 0, sizeof(cde));

	/* Simulate . in every dir except ROOT */
	if (ip->i_number != NTFS_ROOTINO && uio->uio_offset == 0) {
		cde.d_fileno = ip->i_number;
		cde.d_reclen = sizeof(struct dirent);
		cde.d_type = DT_DIR;
		cde.d_namlen = 1;
		cde.d_off = sizeof(struct dirent);
		cde.d_name[0] = '.';
		cde.d_name[1] = '\0';
		error = uiomove(&cde, sizeof(struct dirent), uio);
		if (error)
			goto out;
	}

	/* Simulate .. in every dir including ROOT */
	if (uio->uio_offset < 2 * sizeof(struct dirent)) {
		cde.d_fileno = NTFS_ROOTINO;	/* XXX */
		cde.d_reclen = sizeof(struct dirent);
		cde.d_type = DT_DIR;
		cde.d_namlen = 2;
		cde.d_off = 2 * sizeof(struct dirent);
		cde.d_name[0] = '.';
		cde.d_name[1] = '.';
		cde.d_name[2] = '\0';
		error = uiomove(&cde, sizeof(struct dirent), uio);
		if (error)
			goto out;
	}

	faked = (ip->i_number == NTFS_ROOTINO) ? 1 : 2;
	num = uio->uio_offset / sizeof(struct dirent) - faked;

	while (uio->uio_resid >= sizeof(struct dirent)) {
		struct attr_indexentry *iep;
		char *fname;
		size_t remains;
		int sz;

		error = ntfs_ntreaddir(ntmp, fp, num, &iep, uio->uio_procp);
		if (error)
			goto out;

		if (NULL == iep)
			break;

		for(; !(iep->ie_flag & NTFS_IEFLAG_LAST) && (uio->uio_resid >= sizeof(struct dirent));
			iep = NTFS_NEXTREC(iep, struct attr_indexentry *))
		{
			if(!ntfs_isnamepermitted(ntmp,iep))
				continue;

			remains = sizeof(cde.d_name) - 1;
			fname = cde.d_name;
			for(i=0; i<iep->ie_fnamelen; i++) {
				sz = (*ntmp->ntm_wput)(fname, remains,
						iep->ie_fname[i]);
				fname += sz;
				remains -= sz;
			}
			*fname = '\0';
			DPRINTF("ntfs_readdir: elem: %u, fname:[%s] type: %u, "
			    "flag: %u, ",
			    num, cde.d_name, iep->ie_fnametype, iep->ie_flag);
			cde.d_namlen = fname - (char *) cde.d_name;
			if (memchr(cde.d_name, '/', cde.d_namlen) != NULL) {
				error = EINVAL;
				goto out;
			}
			cde.d_fileno = iep->ie_number;
			cde.d_type = (iep->ie_fflag & NTFS_FFLAG_DIR) ? DT_DIR : DT_REG;
			cde.d_reclen = sizeof(struct dirent);
			cde.d_off = uio->uio_offset + sizeof(struct dirent);
			DPRINTF("%s\n", cde.d_type == DT_DIR ? "dir" : "reg");

			error = uiomove(&cde, sizeof(struct dirent), uio);
			if (error)
				goto out;
			num++;
		}
	}

	DPRINTF("ntfs_readdir: %u entries (%lld bytes) read\n",
	    num, uio->uio_offset - off);
	DPRINTF("ntfs_readdir: off: %lld resid: %zu\n",
	    uio->uio_offset, uio->uio_resid);

/*
	if (ap->a_eofflag)
	    *ap->a_eofflag = VTONT(ap->a_vp)->i_size <= uio->uio_offset;
*/
out:
	if (fp->f_dirblbuf != NULL) {
		free(fp->f_dirblbuf, M_NTFSDIR, 0);
		fp->f_dirblbuf = NULL;
	}
	return (error);
}

int
ntfs_lookup(void *v)
{
	struct vop_lookup_args *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct ntnode *dip = VTONT(dvp);
	struct ntfsmount *ntmp = dip->i_mp;
	struct componentname *cnp = ap->a_cnp;
	struct ucred *cred = cnp->cn_cred;
	int error;
	int lockparent = cnp->cn_flags & LOCKPARENT;
#if NTFS_DEBUG
	int wantparent = cnp->cn_flags & (LOCKPARENT|WANTPARENT);
#endif
	DPRINTF("ntfs_lookup: \"%.*s\" (%ld bytes) in %u, lp: %d, wp: %d \n",
	    (unsigned int)cnp->cn_namelen, cnp->cn_nameptr, cnp->cn_namelen,
	    dip->i_number, lockparent, wantparent);

	error = VOP_ACCESS(dvp, VEXEC, cred, cnp->cn_proc);
	if(error)
		return (error);

	if ((cnp->cn_flags & ISLASTCN) &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME))
		return (EROFS);

	/*
	 * We now have a segment name to search for, and a directory
	 * to search.
	 *
	 * Before tediously performing a linear scan of the directory,
	 * check the name cache to see if the directory/name pair
	 * we are looking for is known already.
	 */
	if ((error = cache_lookup(ap->a_dvp, ap->a_vpp, cnp)) >= 0)
		return (error);

	if(cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
		DPRINTF("ntfs_lookup: faking . directory in %u\n",
		    dip->i_number);

		vref(dvp);
		*ap->a_vpp = dvp;
		error = 0;
	} else if (cnp->cn_flags & ISDOTDOT) {
		struct ntvattr *vap;

		DPRINTF("ntfs_lookup: faking .. directory in %u\n",
		    dip->i_number);

		VOP_UNLOCK(dvp);
		cnp->cn_flags |= PDIRUNLOCK;

		error = ntfs_ntvattrget(ntmp, dip, NTFS_A_NAME, NULL, 0, &vap);
		if(error)
			return (error);

		DPRINTF("ntfs_lookup: parentdir: %u\n",
		    vap->va_a_name->n_pnumber);
		error = VFS_VGET(ntmp->ntm_mountp,
				 vap->va_a_name->n_pnumber,ap->a_vpp); 
		ntfs_ntvattrrele(vap);
		if (error) {
			if (vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY) == 0)
				cnp->cn_flags &= ~PDIRUNLOCK;
			return (error);
		}

		if (lockparent && (cnp->cn_flags & ISLASTCN)) {
			error = vn_lock(dvp, LK_EXCLUSIVE);
			if (error) {
				vput( *(ap->a_vpp) );
				return (error);
			}
			cnp->cn_flags &= ~PDIRUNLOCK;
		}
	} else {
		error = ntfs_ntlookupfile(ntmp, dvp, cnp, ap->a_vpp);
		if (error) {
			DPRINTF("ntfs_ntlookupfile: returned %d\n", error);
			return (error);
		}

		DPRINTF("ntfs_lookup: found ino: %u\n",
		    VTONT(*ap->a_vpp)->i_number);

		if(!lockparent || (cnp->cn_flags & ISLASTCN) == 0) {
			VOP_UNLOCK(dvp);
			cnp->cn_flags |= PDIRUNLOCK;
		}
	}

	if (cnp->cn_flags & MAKEENTRY)
		cache_enter(dvp, *ap->a_vpp, cnp);

	return (error);
}

/*
 * Flush the blocks of a file to disk.
 *
 * This function is worthless for vnodes that represent directories. Maybe we
 * could just do a sync if they try an fsync on a directory file.
 */
int
ntfs_fsync(void *v)
{
	return (0);
}

/*
 * Return POSIX pathconf information applicable to NTFS filesystem
 */
int
ntfs_pathconf(void *v)
{
	struct vop_pathconf_args *ap = v;
	int error = 0;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		break;
	case _PC_NAME_MAX:
		*ap->a_retval = NTFS_MAXFILENAME;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = 0;
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

/*
 * Global vfs data structures
 */
const struct vops ntfs_vops = {
	.vop_getattr	= ntfs_getattr,
	.vop_inactive	= ntfs_inactive,
	.vop_reclaim	= ntfs_reclaim,
	.vop_print	= ntfs_print,
	.vop_pathconf	= ntfs_pathconf,
	.vop_lock	= nullop,
	.vop_unlock	= nullop,
	.vop_islocked	= nullop,
	.vop_lookup	= ntfs_lookup,
	.vop_access	= ntfs_access,
	.vop_close	= ntfs_close,
	.vop_open	= ntfs_open,
	.vop_readdir	= ntfs_readdir,
	.vop_fsync	= ntfs_fsync,
	.vop_bmap	= ntfs_bmap,
	.vop_strategy	= ntfs_strategy,
	.vop_bwrite	= vop_generic_bwrite,
	.vop_read	= ntfs_read,

	.vop_abortop	= NULL,
	.vop_advlock	= NULL,
	.vop_create	= NULL,
	.vop_ioctl	= NULL,
	.vop_link	= NULL,
	.vop_mknod	= NULL,
	.vop_readlink	= NULL,
	.vop_remove	= eopnotsupp,
	.vop_rename	= NULL,
	.vop_revoke	= NULL,
	.vop_mkdir	= NULL,
	.vop_rmdir	= NULL,
	.vop_setattr	= NULL,
	.vop_symlink	= NULL,
	.vop_write	= NULL,
	.vop_kqfilter	= NULL
};
