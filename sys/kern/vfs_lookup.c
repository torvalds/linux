/*	$OpenBSD: vfs_lookup.c,v 1.90 2025/09/20 13:53:36 mpi Exp $	*/
/*	$NetBSD: vfs_lookup.c,v 1.17 1996/02/09 19:00:59 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)vfs_lookup.c	8.6 (Berkeley) 11/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslimits.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/errno.h>
#include <sys/pool.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/pledge.h>
#include <sys/file.h>
#include <sys/fcntl.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

int
component_push(struct componentname *cnp, char *component, size_t len)
{
	if (cnp->cn_rpi + len + 1 >= MAXPATHLEN)
		return 0;
	if (cnp->cn_rpi > 1)
		cnp->cn_rpbuf[cnp->cn_rpi++] = '/';
	memcpy(cnp->cn_rpbuf + cnp->cn_rpi,  component, len);
	cnp->cn_rpi+=len;
	cnp->cn_rpbuf[cnp->cn_rpi] = '\0';
	return 1;
}

void
component_pop(struct componentname *cnp)
{
	while(cnp->cn_rpi && cnp->cn_rpbuf[cnp->cn_rpi] != '/' )
		cnp->cn_rpi--;
	if (cnp->cn_rpi == 0 && cnp->cn_rpbuf[0] == '/')
		cnp->cn_rpi++;
	cnp->cn_rpbuf[cnp->cn_rpi] = '\0';
}

void
ndinitat(struct nameidata *ndp, u_long op, u_long flags,
    enum uio_seg segflg, int dirfd, const char *namep, struct proc *p)
{
	memset(ndp, 0, sizeof(*ndp));
	ndp->ni_cnd.cn_nameiop = op;
	ndp->ni_cnd.cn_flags = flags;
	ndp->ni_segflg = segflg;
	ndp->ni_dirfd = dirfd;
	ndp->ni_dirp = namep;
	ndp->ni_cnd.cn_proc = p;
}

/*
 * Convert a pathname into a pointer to a vnode.
 *
 * The FOLLOW flag is set when symbolic links are to be followed
 * when they occur at the end of the name translation process.
 * Symbolic links are always followed for all other pathname
 * components other than the last.
 *
 * If the LOCKLEAF flag is set, a locked vnode is returned.
 *
 * The segflg defines whether the name is to be copied from user
 * space or kernel space.
 *
 * Overall outline of namei:
 *
 *	copy in name
 *	get starting directory
 *	while (!done && !error) {
 *		call lookup to search path.
 *		if symbolic link, massage name in buffer and continue
 *	}
 */
int
namei(struct nameidata *ndp)
{
	struct filedesc *fdp;		/* pointer to file descriptor state */
	char *cp;			/* pointer into pathname argument */
	struct vnode *dp;		/* the directory we are searching */
	struct iovec aiov;		/* uio for reading symbolic links */
	struct uio auio;
	int error, linklen;
	struct componentname *cnp = &ndp->ni_cnd;
	struct proc *p = cnp->cn_proc;

	ndp->ni_cnd.cn_cred = ndp->ni_cnd.cn_proc->p_ucred;
#ifdef DIAGNOSTIC
	if (!cnp->cn_cred || !cnp->cn_proc)
		panic ("namei: bad cred/proc");
	if (cnp->cn_nameiop & (~OPMASK))
		panic ("namei: nameiop contaminated with flags");
	if (cnp->cn_flags & OPMASK)
		panic ("namei: flags contaminated with nameiops");
#endif
	fdp = cnp->cn_proc->p_fd;

	/*
	 * Get a buffer for the name to be translated, and copy the
	 * name into the buffer.
	 */
	if ((cnp->cn_flags & HASBUF) == 0)
		cnp->cn_pnbuf = pool_get(&namei_pool, PR_WAITOK);
	if (ndp->ni_segflg == UIO_SYSSPACE) {
		ndp->ni_pathlen = strlcpy(cnp->cn_pnbuf, ndp->ni_dirp,
		    MAXPATHLEN);
		if (ndp->ni_pathlen >= MAXPATHLEN) {
			error = ENAMETOOLONG;
		} else {
			error = 0;
			ndp->ni_pathlen++;	/* ni_pathlen includes NUL */
		}
	} else
		error = copyinstr(ndp->ni_dirp, cnp->cn_pnbuf,
			    MAXPATHLEN, &ndp->ni_pathlen);

	/*
	 * Fail on null pathnames
	 */
	if (error == 0 && ndp->ni_pathlen == 1)
		error = ENOENT;

	if (error)
		goto fail;

#ifdef KTRACE
	if (KTRPOINT(cnp->cn_proc, KTR_NAMEI))
		ktrnamei(cnp->cn_proc, cnp->cn_pnbuf);
#endif

	/*
	 *  Strip trailing slashes, as requested
	 */
	if (cnp->cn_flags & STRIPSLASHES) {
		char *end = cnp->cn_pnbuf + ndp->ni_pathlen - 2;

		cp = end;
		while (cp >= cnp->cn_pnbuf && (*cp == '/'))
			cp--;

		/* Still some remaining characters in the buffer */
		if (cp >= cnp->cn_pnbuf) {
			ndp->ni_pathlen -= (end - cp);
			*(cp + 1) = '\0';
		}
	}

	ndp->ni_loopcnt = 0;

	/*
	 * Get starting point for the translation.
	 */
	if ((ndp->ni_rootdir = fdp->fd_rdir) == NULL ||
	    (ndp->ni_cnd.cn_flags & KERNELPATH))
		ndp->ni_rootdir = rootvnode;

	if (ndp->ni_cnd.cn_flags & KERNELPATH) {
		ndp->ni_cnd.cn_flags |= BYPASSUNVEIL;
	} else {
		error = pledge_namei(p, ndp, cnp->cn_pnbuf);
		if (error)
			goto fail;
	}

	/*
	 * Check if starting from root directory or current directory.
	 */
	if (cnp->cn_pnbuf[0] == '/') {
		dp = ndp->ni_rootdir;
		vref(dp);
		if (cnp->cn_flags & REALPATH && cnp->cn_rpi == 0) {
			cnp->cn_rpbuf[0] = '/';
			cnp->cn_rpbuf[1] = '\0';
			cnp->cn_rpi = 1;
		}
	} else if (ndp->ni_dirfd == AT_FDCWD) {
		dp = fdp->fd_cdir;
		vref(dp);
		unveil_start_relative(p, ndp, dp);
		unveil_check_component(p, ndp, dp);
	} else {
		struct file *fp = fd_getfile(fdp, ndp->ni_dirfd);
		if (fp == NULL) {
			error = EBADF;
			goto fail;
		}
		dp = (struct vnode *)fp->f_data;
		if (fp->f_type != DTYPE_VNODE || dp->v_type != VDIR) {
			FRELE(fp, p);
			error = ENOTDIR;
			goto fail;
		}
		vref(dp);
		unveil_start_relative(p, ndp, dp);
		unveil_check_component(p, ndp, dp);
		FRELE(fp, p);
	}
	for (;;) {
		if (!dp->v_mount) {
			/* Give up if the directory is no longer mounted */
			vrele(dp);
			error = ENOENT;
			goto fail;
		}

		cnp->cn_nameptr = cnp->cn_pnbuf;
		ndp->ni_startdir = dp;
		if ((error = vfs_lookup(ndp)) != 0)
			goto fail;

		/*
		 * If not a symbolic link, return search result.
		 */
		if ((cnp->cn_flags & ISSYMLINK) == 0) {
			if ((error = unveil_check_final(p, ndp))) {
				if ((cnp->cn_flags & LOCKPARENT) &&
				    (cnp->cn_flags & ISLASTCN) &&
				    (ndp->ni_vp != ndp->ni_dvp))
					vput(ndp->ni_dvp);
				if (ndp->ni_vp) {
					if ((cnp->cn_flags & LOCKLEAF))
						vput(ndp->ni_vp);
					else
						vrele(ndp->ni_vp);
				}
				goto fail;
			}
			if ((cnp->cn_flags & (SAVENAME | SAVESTART)) == 0)
				pool_put(&namei_pool, cnp->cn_pnbuf);
			else
				cnp->cn_flags |= HASBUF;
			return (0);
		}
		if ((cnp->cn_flags & LOCKPARENT) && (cnp->cn_flags & ISLASTCN))
			VOP_UNLOCK(ndp->ni_dvp);
		if (ndp->ni_loopcnt++ >= SYMLOOP_MAX) {
			error = ELOOP;
			break;
		}
		if (ndp->ni_pathlen > 1)
			cp = pool_get(&namei_pool, PR_WAITOK);
		else
			cp = cnp->cn_pnbuf;
		aiov.iov_base = cp;
		aiov.iov_len = MAXPATHLEN;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_procp = cnp->cn_proc;
		auio.uio_resid = MAXPATHLEN;
		error = VOP_READLINK(ndp->ni_vp, &auio, cnp->cn_cred);
		if (error) {
badlink:
			if (ndp->ni_pathlen > 1)
				pool_put(&namei_pool, cp);
			break;
		}
		linklen = MAXPATHLEN - auio.uio_resid;
		if (linklen == 0) {
			error = ENOENT;
			goto badlink;
		}
		if (linklen + ndp->ni_pathlen >= MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto badlink;
		}
		if (ndp->ni_pathlen > 1) {
			memcpy(cp + linklen, ndp->ni_next, ndp->ni_pathlen);
			pool_put(&namei_pool, cnp->cn_pnbuf);
			cnp->cn_pnbuf = cp;
		} else
			cnp->cn_pnbuf[linklen] = '\0';
		ndp->ni_pathlen += linklen;
		vput(ndp->ni_vp);
		dp = ndp->ni_dvp;
		/*
		 * Check if root directory should replace current directory.
		 */
		if (cnp->cn_pnbuf[0] == '/') {
			vrele(dp);
			dp = ndp->ni_rootdir;
			vref(dp);
			ndp->ni_unveil_match = NULL;
			unveil_check_component(p, ndp, dp);
			if (cnp->cn_flags & REALPATH) {
				cnp->cn_rpbuf[0] = '/';
				cnp->cn_rpbuf[1] = '\0';
				cnp->cn_rpi = 1;
			}
		} else if (cnp->cn_flags & REALPATH) {
			component_pop(cnp);
		}
	}
	vrele(ndp->ni_dvp);
	vput(ndp->ni_vp);
fail:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	ndp->ni_vp = NULL;
	return (error);
}

/*
 * Search a pathname.
 * This is a very central and rather complicated routine.
 *
 * The pathname is pointed to by ni_cnd.cn_nameptr and is of length
 * ni_pathlen.  The starting directory is taken from ni_startdir. The
 * pathname is descended until done, or a symbolic link is encountered.
 * If the path is completed the flag ISLASTCN is set in ni_cnd.cn_flags.
 * If a symbolic link need interpretation is encountered, the flag ISSYMLINK
 * is set in ni_cnd.cn_flags.
 *
 * The flag argument is LOOKUP, CREATE, RENAME, or DELETE depending on
 * whether the name is to be looked up, created, renamed, or deleted.
 * When CREATE, RENAME, or DELETE is specified, information usable in
 * creating, renaming, or deleting a directory entry may be calculated.
 * If flag has LOCKPARENT or'ed into it, the parent directory is returned
 * locked. If flag has WANTPARENT or'ed into it, the parent directory is
 * returned unlocked. Otherwise the parent directory is not returned. If
 * the target of the pathname exists and LOCKLEAF is or'ed into the flag
 * the target is returned locked, otherwise it is returned unlocked.
 * When creating or renaming and LOCKPARENT is specified, the target may not
 * be ".".  When deleting and LOCKPARENT is specified, the target may be ".".
 *
 * Overall outline of lookup:
 *
 * dirloop:
 *	identify next component of name at ndp->ni_ptr
 *	handle degenerate case where name is null string
 *	if .. and crossing mount points and on mounted filesys, find parent
 *	call VOP_LOOKUP routine for next component name
 *	    directory vnode returned in ni_dvp, unlocked unless LOCKPARENT set
 *	    component vnode returned in ni_vp (if it exists), locked.
 *	if result vnode is mounted on and crossing mount points,
 *	    find mounted on vnode
 *	if more components of name, do next level at dirloop
 *	return the answer in ni_vp, locked if LOCKLEAF set
 *	    if LOCKPARENT set, return locked parent in ni_dvp
 *	    if WANTPARENT set, return unlocked parent in ni_dvp
 */
int
vfs_lookup(struct nameidata *ndp)
{
	char *cp;			/* pointer into pathname argument */
	struct vnode *dp = NULL;	/* the directory we are searching */
	struct vnode *tdp;		/* saved dp */
	struct mount *mp;		/* mount table entry */
	int docache;			/* == 0 do not cache last component */
	int wantparent;			/* 1 => wantparent or lockparent flag */
	int rdonly;			/* lookup read-only flag bit */
	int error = 0;
	int dpunlocked = 0;		/* dp has already been unlocked */
	int slashes;
	struct componentname *cnp = &ndp->ni_cnd;
	/*
	 * Setup: break out flag bits into variables.
	 */
	wantparent = cnp->cn_flags & (LOCKPARENT | WANTPARENT);
	docache = (cnp->cn_flags & NOCACHE) ^ NOCACHE;
	if (cnp->cn_nameiop == DELETE ||
	    (wantparent && cnp->cn_nameiop != CREATE))
		docache = 0;
	rdonly = cnp->cn_flags & RDONLY;
	ndp->ni_dvp = NULL;
	cnp->cn_flags &= ~ISSYMLINK;
	dp = ndp->ni_startdir;
	ndp->ni_startdir = NULL;
	vn_lock(dp, LK_EXCLUSIVE | LK_RETRY);

	/*
	 * If we have a leading string of slashes, remove them, and just make
	 * sure the current node is a directory.
	 */
	cp = cnp->cn_nameptr;
	if (*cp == '/') {
		do {
			cp++;
		} while (*cp == '/');
		ndp->ni_pathlen -= cp - cnp->cn_nameptr;
		cnp->cn_nameptr = cp;

		if (dp->v_type != VDIR) {
			error = ENOTDIR;
			goto bad;
		}

		/*
		 * If we've exhausted the path name, then just return the
		 * current node.  If the caller requested the parent node (i.e.
		 * it's a CREATE, DELETE, or RENAME), and we don't have one
		 * (because this is the root directory), then we must fail.
		 */
		if (cnp->cn_nameptr[0] == '\0') {
			if (ndp->ni_dvp == NULL && wantparent) {
				error = EISDIR;
				goto bad;
			}
			ndp->ni_vp = dp;
			cnp->cn_flags |= ISLASTCN;
			goto terminal;
		}
	}

dirloop:
	/*
	 * Search a new directory.
	 *
	 * The last component of the filename is left accessible via
	 * cnp->cn_nameptr for callers that need the name. Callers needing
	 * the name set the SAVENAME flag. When done, they assume
	 * responsibility for freeing the pathname buffer.
	 */
	cnp->cn_consume = 0;

	/* XXX: Figure out the length of the last component. */
	cp = cnp->cn_nameptr;
	while (*cp && (*cp != '/'))
		cp++;
	cnp->cn_namelen = cp - cnp->cn_nameptr;
	if (cnp->cn_namelen > NAME_MAX) {
		error = ENAMETOOLONG;
		goto bad;
	}

#ifdef NAMEI_DIAGNOSTIC
	{ char c = *cp;
	*cp = '\0';
	printf("{%s}: ", cnp->cn_nameptr);
	*cp = c; }
#endif
	if (cnp->cn_flags & REALPATH) {
		size_t len = cp - cnp->cn_nameptr;
		if (len == 2 && cnp->cn_nameptr[0] == '.' &&
		    cnp->cn_nameptr[1] == '.')
			component_pop(cnp);
		else if (!(len == 1 && cnp->cn_nameptr[0] == '.')) {
			if (!component_push(cnp, cnp->cn_nameptr, len)) {
				error = ENAMETOOLONG;
				goto bad;
			}
		}
	}

	ndp->ni_pathlen -= cnp->cn_namelen;
	ndp->ni_next = cp;
	/*
	 * If this component is followed by a slash, then move the pointer to
	 * the next component forward, and remember that this component must be
	 * a directory.
	 */
	if (*cp == '/') {
		do {
			cp++;
		} while (*cp == '/');
		slashes = cp - ndp->ni_next;
		ndp->ni_pathlen -= slashes;
		ndp->ni_next = cp;
		cnp->cn_flags |= REQUIREDIR;
	} else {
		slashes = 0;
		cnp->cn_flags &= ~REQUIREDIR;
	}
	/*
	 * We do special processing on the last component, whether or not it's
	 * a directory.  Cache all intervening lookups, but not the final one.
	 */
	if (*cp == '\0') {
		if (docache)
			cnp->cn_flags |= MAKEENTRY;
		else
			cnp->cn_flags &= ~MAKEENTRY;
		cnp->cn_flags |= ISLASTCN;
	} else {
		cnp->cn_flags |= MAKEENTRY;
		cnp->cn_flags &= ~ISLASTCN;
	}
	if (cnp->cn_namelen == 2 &&
	    cnp->cn_nameptr[1] == '.' && cnp->cn_nameptr[0] == '.')
		cnp->cn_flags |= ISDOTDOT;
	else
		cnp->cn_flags &= ~ISDOTDOT;

	/*
	 * Handle "..": two special cases.
	 * 1. If at root directory (e.g. after chroot)
	 *    or at absolute root directory
	 *    then ignore it so can't get out.
	 * 2. If this vnode is the root of a mounted
	 *    filesystem, then replace it with the
	 *    vnode which was mounted on so we take the
	 *    .. in the other file system.
	 */
	if (cnp->cn_flags & ISDOTDOT) {
		for (;;) {
			if (dp == ndp->ni_rootdir || dp == rootvnode) {
				ndp->ni_dvp = dp;
				ndp->ni_vp = dp;
				vref(dp);
				ndp->ni_unveil_match = NULL;
				goto nextname;
			}
			if ((dp->v_flag & VROOT) == 0 ||
			    (cnp->cn_flags & NOCROSSMOUNT))
				break;
			tdp = dp;
			dp = dp->v_mount->mnt_vnodecovered;
			vput(tdp);
			vref(dp);
			unveil_check_component(curproc, ndp, dp);
			vn_lock(dp, LK_EXCLUSIVE | LK_RETRY);
		}
	}

	/*
	 * We now have a segment name to search for, and a directory to search.
	 */
	ndp->ni_dvp = dp;
	ndp->ni_vp = NULL;
	cnp->cn_flags &= ~PDIRUNLOCK;
	unveil_check_component(curproc, ndp, dp);

	if ((error = VOP_LOOKUP(dp, &ndp->ni_vp, cnp)) != 0) {
#ifdef DIAGNOSTIC
		if (ndp->ni_vp != NULL)
			panic("leaf should be empty");
#endif
#ifdef NAMEI_DIAGNOSTIC
		printf("not found\n");
#endif
		/*
		 * Allow for unveiling a file in a directory which we cannot
		 * create ourselves.
		 */
		if (ndp->ni_pledge == PLEDGE_UNVEIL &&
		    (error == EPERM || error == EACCES || error == EROFS))
			error = EJUSTRETURN;

		if (error != EJUSTRETURN)
			goto bad;
		/*
		 * If this was not the last component, or there were trailing
		 * slashes, then the name must exist.
		 */
		if (cnp->cn_flags & REQUIREDIR) {
			error = ENOENT;
			goto bad;
		}
		/*
		 * If creating and at end of pathname, then can consider
		 * allowing file to be created. Check for a read only
		 * filesystem and disallow this unless we are unveil'ing
		 */
		if (ndp->ni_pledge != PLEDGE_UNVEIL && (rdonly ||
		    (ndp->ni_dvp->v_mount->mnt_flag & MNT_RDONLY))) {
			    error = EROFS;
			    goto bad;
		}
		/*
		 * We return with ni_vp NULL to indicate that the entry
		 * doesn't currently exist, leaving a pointer to the
		 * (possibly locked) directory vnode in ndp->ni_dvp.
		 */
		if (cnp->cn_flags & SAVESTART) {
			ndp->ni_startdir = ndp->ni_dvp;
			vref(ndp->ni_startdir);
		}
		return (0);
	}
#ifdef NAMEI_DIAGNOSTIC
		printf("found\n");
#endif

	/*
	 * Take into account any additional components consumed by the
	 * underlying filesystem.  This will include any trailing slashes after
	 * the last component consumed.
	 */
	if (cnp->cn_consume > 0) {
		if (cnp->cn_consume >= slashes) {
			cnp->cn_flags &= ~REQUIREDIR;
		}

		ndp->ni_pathlen -= cnp->cn_consume - slashes;
		ndp->ni_next += cnp->cn_consume - slashes;
		cnp->cn_consume = 0;
		if (ndp->ni_next[0] == '\0')
			cnp->cn_flags |= ISLASTCN;
	}

	dp = ndp->ni_vp;
	/*
	 * Check to see if the vnode has been mounted on;
	 * if so find the root of the mounted file system.
	 */
	while (dp->v_type == VDIR && (mp = dp->v_mountedhere) &&
	    (cnp->cn_flags & NOCROSSMOUNT) == 0) {
		if (vfs_busy(mp, VB_READ|VB_WAIT))
			continue;
		VOP_UNLOCK(dp);
		error = VFS_ROOT(mp, &tdp);
		vfs_unbusy(mp);
		if (error) {
			dpunlocked = 1;
			goto bad2;
		}
		vrele(dp);
		ndp->ni_vp = dp = tdp;
	}

	/*
	 * Check for symbolic link.  Back up over any slashes that we skipped,
	 * as we will need them again.
	 */
	if ((dp->v_type == VLNK) && (cnp->cn_flags & (FOLLOW|REQUIREDIR))) {
		ndp->ni_pathlen += slashes;
		ndp->ni_next -= slashes;
		cnp->cn_flags |= ISSYMLINK;
		return (0);
	}

	/*
	 * Check for directory, if the component was followed by a series of
	 * slashes.
	 */
	if ((dp->v_type != VDIR) && (cnp->cn_flags & REQUIREDIR)) {
		error = ENOTDIR;
		goto bad2;
	}

nextname:
	/*
	 * Not a symbolic link.  If this was not the last component, then
	 * continue at the next component, else return.
	 */
	if (!(cnp->cn_flags & ISLASTCN)) {
		cnp->cn_nameptr = ndp->ni_next;
		vrele(ndp->ni_dvp);
		goto dirloop;
	}

terminal:
	/*
	 * Check for read-only file systems.
	 */
	if (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME) {
		/*
		 * Disallow directory write attempts on read-only
		 * file systems.
		 */
		if (rdonly || (dp->v_mount->mnt_flag & MNT_RDONLY) ||
		    (wantparent &&
		    (ndp->ni_dvp->v_mount->mnt_flag & MNT_RDONLY))) {
			error = EROFS;
			goto bad2;
		}
	}
	if (ndp->ni_dvp != NULL) {
		if (cnp->cn_flags & SAVESTART) {
			ndp->ni_startdir = ndp->ni_dvp;
			vref(ndp->ni_startdir);
		}
		if (!wantparent)
			vrele(ndp->ni_dvp);
	}
	if ((cnp->cn_flags & LOCKLEAF) == 0)
		VOP_UNLOCK(dp);
	return (0);

bad2:
	if ((cnp->cn_flags & LOCKPARENT) && (cnp->cn_flags & ISLASTCN) &&
	    ((cnp->cn_flags & PDIRUNLOCK) == 0))
		VOP_UNLOCK(ndp->ni_dvp);
	vrele(ndp->ni_dvp);
bad:
	if (dpunlocked)
		vrele(dp);
	else
		vput(dp);
	ndp->ni_vp = NULL;
	return (error);
}

/*
 * Reacquire a path name component.
 */
int
vfs_relookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp)
{
	struct vnode *dp = NULL;	/* the directory we are searching */
	int wantparent;			/* 1 => wantparent or lockparent flag */
	int rdonly;			/* lookup read-only flag bit */
	int error = 0;
#ifdef NAMEI_DIAGNOSTIC
	char *cp;			/* DEBUG: check name ptr/len */
#endif

	/*
	 * Setup: break out flag bits into variables.
	 */
	wantparent = cnp->cn_flags & (LOCKPARENT|WANTPARENT);
	rdonly = cnp->cn_flags & RDONLY;
	cnp->cn_flags &= ~ISSYMLINK;
	dp = dvp;
	vn_lock(dp, LK_EXCLUSIVE | LK_RETRY);

/* dirloop: */
	/*
	 * Search a new directory.
	 *
	 * The last component of the filename is left accessible via
	 * cnp->cn_nameptr for callers that need the name. Callers needing
	 * the name set the SAVENAME flag. When done, they assume
	 * responsibility for freeing the pathname buffer.
	 */

#ifdef NAMEI_DIAGNOSTIC
	/* XXX: Figure out the length of the last component. */
	cp = cnp->cn_nameptr;
	while (*cp && (*cp != '/')) {
		cp++;
	}
	if (cnp->cn_namelen != cp - cnp->cn_nameptr)
		panic("relookup: bad len");
	if (*cp != 0)
		panic("relookup: not last component");
	printf("{%s}: ", cnp->cn_nameptr);
#endif

	/*
	 * Check for degenerate name (e.g. / or "")
	 * which is a way of talking about a directory,
	 * e.g. like "/." or ".".
	 */
	if (cnp->cn_nameptr[0] == '\0')
		panic("relookup: null name");

	if (cnp->cn_flags & ISDOTDOT)
		panic ("relookup: lookup on dot-dot");

	/*
	 * We now have a segment name to search for, and a directory to search.
	 */
	if ((error = VOP_LOOKUP(dp, vpp, cnp)) != 0) {
#ifdef DIAGNOSTIC
		if (*vpp != NULL)
			panic("leaf should be empty");
#endif
		if (error != EJUSTRETURN)
			goto bad;
		/*
		 * If creating and at end of pathname, then can consider
		 * allowing file to be created.
		 */
		if (rdonly || (dvp->v_mount->mnt_flag & MNT_RDONLY)) {
			error = EROFS;
			goto bad;
		}
		/* ASSERT(dvp == ndp->ni_startdir) */
		if (cnp->cn_flags & SAVESTART)
			vref(dvp);
		/*
		 * We return with ni_vp NULL to indicate that the entry
		 * doesn't currently exist, leaving a pointer to the
		 * (possibly locked) directory vnode in ndp->ni_dvp.
		 */
		return (0);
	}
	dp = *vpp;

#ifdef DIAGNOSTIC
	/*
	 * Check for symbolic link
	 */
	if (dp->v_type == VLNK && (cnp->cn_flags & FOLLOW))
		panic ("relookup: symlink found.");
#endif

	/*
	 * Check for read-only file systems.
	 */
	if (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME) {
		/*
		 * Disallow directory write attempts on read-only
		 * file systems.
		 */
		if (rdonly || (dp->v_mount->mnt_flag & MNT_RDONLY) ||
		    (wantparent &&
		    (dvp->v_mount->mnt_flag & MNT_RDONLY))) {
			error = EROFS;
			goto bad2;
		}
	}
	/* ASSERT(dvp == ndp->ni_startdir) */
	if (cnp->cn_flags & SAVESTART)
		vref(dvp);
	if (!wantparent)
		vrele(dvp);
	if ((cnp->cn_flags & LOCKLEAF) == 0)
		VOP_UNLOCK(dp);
	return (0);

bad2:
	if ((cnp->cn_flags & LOCKPARENT) && (cnp->cn_flags & ISLASTCN))
		VOP_UNLOCK(dvp);
	vrele(dvp);
bad:
	vput(dp);
	*vpp = NULL;
	return (error);
}


