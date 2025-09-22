/* $OpenBSD: vfs_getcwd.c,v 1.38 2022/12/05 23:18:37 deraadt Exp $ */
/* $NetBSD: vfs_getcwd.c,v 1.3.2.3 1999/07/11 10:24:09 sommerfeld Exp $ */

/*
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Bill Sommerfeld.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/filedesc.h>
#include <sys/stat.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/ktrace.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <ufs/ufs/dir.h>	/* only for DIRBLKSIZ */

#include <sys/syscallargs.h>


/* Find parent vnode of *lvpp, return in *uvpp */
int
vfs_getcwd_scandir(struct vnode **lvpp, struct vnode **uvpp, char **bpp,
    char *bufp, struct proc *p)
{
	int eofflag, tries, dirbuflen = 0, len, reclen, error = 0;
	off_t off;
	struct uio uio;
	struct iovec iov;
	char *dirbuf = NULL;
	ino_t fileno;
	struct vattr va;
	struct vnode *uvp = NULL;
	struct vnode *lvp = *lvpp;
	struct componentname cn;

	tries = 0;

	/*
	 * If we want the filename, get some info we need while the
	 * current directory is still locked.
	 */
	if (bufp != NULL) {
		error = VOP_GETATTR(lvp, &va, p->p_ucred, p);
		if (error) {
			vput(lvp);
			*lvpp = NULL;
			*uvpp = NULL;
			return (error);
		}
	}

	cn.cn_nameiop = LOOKUP;
	cn.cn_flags = ISLASTCN | ISDOTDOT | RDONLY;
	cn.cn_proc = p;
	cn.cn_cred = p->p_ucred;
	cn.cn_pnbuf = NULL;
	cn.cn_nameptr = "..";
	cn.cn_namelen = 2;
	cn.cn_consume = 0;

	/* Get parent vnode using lookup of '..' */
	error = VOP_LOOKUP(lvp, uvpp, &cn);
	if (error) {
		vput(lvp);
		*lvpp = NULL;
		*uvpp = NULL;
		return (error);
	}

	uvp = *uvpp;

	/* If we don't care about the pathname, we're done */
	if (bufp == NULL) {
		error = 0;
		goto out;
	}

	fileno = va.va_fileid;

	dirbuflen = DIRBLKSIZ;
	if (dirbuflen < va.va_blocksize)
		dirbuflen = va.va_blocksize;
	/* XXX we need some limit for fuse, 1 MB should be enough */
	if (dirbuflen > 0xfffff) {
		error = EINVAL;
		goto out;
	}
	dirbuf = malloc(dirbuflen, M_TEMP, M_WAITOK);

	off = 0;

	do {
		char   *cpos;
		struct dirent *dp;

		iov.iov_base = dirbuf;
		iov.iov_len = dirbuflen;

		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = off;
		uio.uio_resid = dirbuflen;
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_rw = UIO_READ;
		uio.uio_procp = p;

		eofflag = 0;

		/* Call VOP_READDIR of parent */
		error = VOP_READDIR(uvp, &uio, p->p_ucred, &eofflag);

		off = uio.uio_offset;

		/* Try again if NFS tosses its cookies */
		if (error == EINVAL && tries < 3) {
			tries++;
			off = 0;
			continue;
		} else if (error) {
			goto out; /* Old userland getcwd() behaviour */
		}

		cpos = dirbuf;
		tries = 0;

		/* Scan directory page looking for matching vnode */ 
		for (len = (dirbuflen - uio.uio_resid); len > 0;
		     len -= reclen) {
			dp = (struct dirent *)cpos;
			reclen = dp->d_reclen;

			/* Check for malformed directory */
			if (reclen < DIRENT_RECSIZE(1) || reclen > len) {
				error = EINVAL;
				goto out;
			}

			if (dp->d_fileno == fileno) {
				char *bp = *bpp;

				if (offsetof(struct dirent, d_name) +
				    dp->d_namlen > reclen) {
					error = EINVAL;
					goto out;
				}
				bp -= dp->d_namlen;
				if (bp <= bufp) {
					error = ERANGE;
					goto out;
				}

				memmove(bp, dp->d_name, dp->d_namlen);
				error = 0;
				*bpp = bp;

				goto out;
			}

			cpos += reclen;
		}

	} while (!eofflag);

	error = ENOENT;

out:

	vrele(lvp);
	*lvpp = NULL;

	free(dirbuf, M_TEMP, dirbuflen);

	return (error);
}

/* Do a lookup in the vnode-to-name reverse */
int
vfs_getcwd_getcache(struct vnode **lvpp, struct vnode **uvpp, char **bpp,
    char *bufp)
{
	struct vnode *lvp, *uvp = NULL;
	char *obp;
	int error, vpid;

	lvp = *lvpp;
	obp = *bpp;	/* Save original position to restore to on error */

	error = cache_revlookup(lvp, uvpp, bpp, bufp);
	if (error) {
		if (error != -1) {
			vput(lvp);
			*lvpp = NULL;
			*uvpp = NULL;
		}

		return (error);
	}

	uvp = *uvpp;
	vpid = uvp->v_id;


	/* Release current lock before acquiring the parent lock */
	VOP_UNLOCK(lvp);

	error = vget(uvp, LK_EXCLUSIVE | LK_RETRY);
	if (error)
		*uvpp = NULL;

	/*
	 * Verify that vget() succeeded, and check that vnode capability
	 * didn't change while we were waiting for the lock.
	 */
	if (error || (vpid != uvp->v_id)) {
		/*
		 * Try to get our lock back. If that works, tell the caller to
		 * try things the hard way, otherwise give up.
		 */
		if (!error)
			vput(uvp);

		*uvpp = NULL;

		error = vn_lock(lvp, LK_EXCLUSIVE | LK_RETRY);
		if (!error) {
			*bpp = obp; /* restore the buffer */
			return (-1);
		}
	}

	vrele(lvp);
	*lvpp = NULL;

	return (error);
}

/* Common routine shared by sys___getcwd() and vn_isunder() and sys___realpath() */
int
vfs_getcwd_common(struct vnode *lvp, struct vnode *rvp, char **bpp, char *bufp,
    int limit, int flags, struct proc *p)
{
	struct filedesc *fdp = p->p_fd;
	struct vnode *uvp = NULL;
	char *bp = NULL;
	int error, perms = VEXEC;

	if (rvp == NULL) {
		rvp = fdp->fd_rdir;
		if (rvp == NULL)
			rvp = rootvnode;
	}

	vref(rvp);
	vref(lvp);

	error = vn_lock(lvp, LK_EXCLUSIVE | LK_RETRY);
	if (error) {
		vrele(lvp);
		lvp = NULL;
		goto out;
	}

	if (bufp)
		bp = *bpp;

	if (lvp == rvp) {
		if (bp)
			*(--bp) = '/';
		goto out;
	}

	/*
	 * This loop will terminate when we hit the root, VOP_READDIR() or
	 * VOP_LOOKUP() fails, or we run out of space in the user buffer.
	 */
	do {
		if (lvp->v_type != VDIR) {
			error = ENOTDIR;
			goto out;
		}

		/* Check for access if caller cares */
		if (flags & GETCWD_CHECK_ACCESS) {
			error = VOP_ACCESS(lvp, perms, p->p_ucred, p);
			if (error)
				goto out;
			perms = VEXEC|VREAD;
		}

		/* Step up if we're a covered vnode */
		while (lvp->v_flag & VROOT) {
			struct vnode *tvp;

			if (lvp == rvp)
				goto out;

			tvp = lvp;
			lvp = lvp->v_mount->mnt_vnodecovered;

			vput(tvp);

			if (lvp == NULL) {
				error = ENOENT;
				goto out;
			}

			vref(lvp);

			error = vn_lock(lvp, LK_EXCLUSIVE | LK_RETRY);
			if (error) {
				vrele(lvp);
				lvp = NULL;
				goto out;
			}
		}

		/* Look in the name cache */
		error = vfs_getcwd_getcache(&lvp, &uvp, &bp, bufp);

		if (error == -1) {
			/* If that fails, look in the directory */
			error = vfs_getcwd_scandir(&lvp, &uvp, &bp, bufp, p);
		}

		if (error)
			goto out;

#ifdef DIAGNOSTIC
		if (lvp != NULL)
			panic("getcwd: oops, forgot to null lvp");
		if (bufp && (bp <= bufp)) {
			panic("getcwd: oops, went back too far");
		}
#endif

		if (bp)
			*(--bp) = '/';

		lvp = uvp;
		uvp = NULL;
		limit--;

	} while ((lvp != rvp) && (limit > 0)); 

out:

	if (bpp)
		*bpp = bp;

	if (uvp)
		vput(uvp);

	if (lvp)
		vput(lvp);

	vrele(rvp);

	return (error);
}

/* Find pathname of a process's current directory */
int
sys___getcwd(struct proc *p, void *v, register_t *retval) 
{
	struct sys___getcwd_args *uap = v;
	int error, len = SCARG(uap, len);
	char *path, *bp;

	if (len > MAXPATHLEN * 4)
		len = MAXPATHLEN * 4;
	else if (len < 2)
		return (ERANGE);

	path = malloc(len, M_TEMP, M_WAITOK);

	bp = &path[len - 1];
	*bp = '\0';

	/*
	 * 5th argument here is "max number of vnodes to traverse".
	 * Since each entry takes up at least 2 bytes in the output
	 * buffer, limit it to N/2 vnodes for an N byte buffer.
	 */
	error = vfs_getcwd_common(p->p_fd->fd_cdir, NULL, &bp, path, len/2,
	    GETCWD_CHECK_ACCESS, p);

	if (error)
		goto out;

	/* Put the result into user buffer */
	error = copyoutstr(bp, SCARG(uap, buf), MAXPATHLEN, NULL);

#ifdef KTRACE
	if (KTRPOINT(p, KTR_NAMEI))
		ktrnamei(p, bp);
#endif

out:
	free(path, M_TEMP, len);

	return (error);
}
