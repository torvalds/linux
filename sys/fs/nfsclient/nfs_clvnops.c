/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	from nfs_vnops.c	8.16 (Berkeley) 5/27/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * vnode op calls for Sun NFS version 2, 3 and 4
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/jail.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/namei.h>
#include <sys/socket.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/lockf.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/signalvar.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>

#include <fs/nfs/nfsport.h>
#include <fs/nfsclient/nfsnode.h>
#include <fs/nfsclient/nfsmount.h>
#include <fs/nfsclient/nfs.h>
#include <fs/nfsclient/nfs_kdtrace.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_var.h>

#include <nfs/nfs_lock.h>

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>

dtrace_nfsclient_accesscache_flush_probe_func_t
		dtrace_nfscl_accesscache_flush_done_probe;
uint32_t	nfscl_accesscache_flush_done_id;

dtrace_nfsclient_accesscache_get_probe_func_t
		dtrace_nfscl_accesscache_get_hit_probe,
		dtrace_nfscl_accesscache_get_miss_probe;
uint32_t	nfscl_accesscache_get_hit_id;
uint32_t	nfscl_accesscache_get_miss_id;

dtrace_nfsclient_accesscache_load_probe_func_t
		dtrace_nfscl_accesscache_load_done_probe;
uint32_t	nfscl_accesscache_load_done_id;
#endif /* !KDTRACE_HOOKS */

/* Defs */
#define	TRUE	1
#define	FALSE	0

extern struct nfsstatsv1 nfsstatsv1;
extern int nfsrv_useacl;
extern int nfscl_debuglevel;
MALLOC_DECLARE(M_NEWNFSREQ);

static vop_read_t	nfsfifo_read;
static vop_write_t	nfsfifo_write;
static vop_close_t	nfsfifo_close;
static int	nfs_setattrrpc(struct vnode *, struct vattr *, struct ucred *,
		    struct thread *);
static vop_lookup_t	nfs_lookup;
static vop_create_t	nfs_create;
static vop_mknod_t	nfs_mknod;
static vop_open_t	nfs_open;
static vop_pathconf_t	nfs_pathconf;
static vop_close_t	nfs_close;
static vop_access_t	nfs_access;
static vop_getattr_t	nfs_getattr;
static vop_setattr_t	nfs_setattr;
static vop_read_t	nfs_read;
static vop_fsync_t	nfs_fsync;
static vop_remove_t	nfs_remove;
static vop_link_t	nfs_link;
static vop_rename_t	nfs_rename;
static vop_mkdir_t	nfs_mkdir;
static vop_rmdir_t	nfs_rmdir;
static vop_symlink_t	nfs_symlink;
static vop_readdir_t	nfs_readdir;
static vop_strategy_t	nfs_strategy;
static	int	nfs_lookitup(struct vnode *, char *, int,
		    struct ucred *, struct thread *, struct nfsnode **);
static	int	nfs_sillyrename(struct vnode *, struct vnode *,
		    struct componentname *);
static vop_access_t	nfsspec_access;
static vop_readlink_t	nfs_readlink;
static vop_print_t	nfs_print;
static vop_advlock_t	nfs_advlock;
static vop_advlockasync_t nfs_advlockasync;
static vop_getacl_t nfs_getacl;
static vop_setacl_t nfs_setacl;
static vop_set_text_t nfs_set_text;

/*
 * Global vfs data structures for nfs
 */

static struct vop_vector newnfs_vnodeops_nosig = {
	.vop_default =		&default_vnodeops,
	.vop_access =		nfs_access,
	.vop_advlock =		nfs_advlock,
	.vop_advlockasync =	nfs_advlockasync,
	.vop_close =		nfs_close,
	.vop_create =		nfs_create,
	.vop_fsync =		nfs_fsync,
	.vop_getattr =		nfs_getattr,
	.vop_getpages =		ncl_getpages,
	.vop_putpages =		ncl_putpages,
	.vop_inactive =		ncl_inactive,
	.vop_link =		nfs_link,
	.vop_lookup =		nfs_lookup,
	.vop_mkdir =		nfs_mkdir,
	.vop_mknod =		nfs_mknod,
	.vop_open =		nfs_open,
	.vop_pathconf =		nfs_pathconf,
	.vop_print =		nfs_print,
	.vop_read =		nfs_read,
	.vop_readdir =		nfs_readdir,
	.vop_readlink =		nfs_readlink,
	.vop_reclaim =		ncl_reclaim,
	.vop_remove =		nfs_remove,
	.vop_rename =		nfs_rename,
	.vop_rmdir =		nfs_rmdir,
	.vop_setattr =		nfs_setattr,
	.vop_strategy =		nfs_strategy,
	.vop_symlink =		nfs_symlink,
	.vop_write =		ncl_write,
	.vop_getacl =		nfs_getacl,
	.vop_setacl =		nfs_setacl,
	.vop_set_text =		nfs_set_text,
};

static int
nfs_vnodeops_bypass(struct vop_generic_args *a)
{

	return (vop_sigdefer(&newnfs_vnodeops_nosig, a));
}

struct vop_vector newnfs_vnodeops = {
	.vop_default =		&default_vnodeops,
	.vop_bypass =		nfs_vnodeops_bypass,
};

static struct vop_vector newnfs_fifoops_nosig = {
	.vop_default =		&fifo_specops,
	.vop_access =		nfsspec_access,
	.vop_close =		nfsfifo_close,
	.vop_fsync =		nfs_fsync,
	.vop_getattr =		nfs_getattr,
	.vop_inactive =		ncl_inactive,
	.vop_pathconf =		nfs_pathconf,
	.vop_print =		nfs_print,
	.vop_read =		nfsfifo_read,
	.vop_reclaim =		ncl_reclaim,
	.vop_setattr =		nfs_setattr,
	.vop_write =		nfsfifo_write,
};

static int
nfs_fifoops_bypass(struct vop_generic_args *a)
{

	return (vop_sigdefer(&newnfs_fifoops_nosig, a));
}

struct vop_vector newnfs_fifoops = {
	.vop_default =		&default_vnodeops,
	.vop_bypass =		nfs_fifoops_bypass,
};

static int nfs_mknodrpc(struct vnode *dvp, struct vnode **vpp,
    struct componentname *cnp, struct vattr *vap);
static int nfs_removerpc(struct vnode *dvp, struct vnode *vp, char *name,
    int namelen, struct ucred *cred, struct thread *td);
static int nfs_renamerpc(struct vnode *fdvp, struct vnode *fvp,
    char *fnameptr, int fnamelen, struct vnode *tdvp, struct vnode *tvp,
    char *tnameptr, int tnamelen, struct ucred *cred, struct thread *td);
static int nfs_renameit(struct vnode *sdvp, struct vnode *svp,
    struct componentname *scnp, struct sillyrename *sp);

/*
 * Global variables
 */
SYSCTL_DECL(_vfs_nfs);

static int	nfsaccess_cache_timeout = NFS_MAXATTRTIMO;
SYSCTL_INT(_vfs_nfs, OID_AUTO, access_cache_timeout, CTLFLAG_RW,
	   &nfsaccess_cache_timeout, 0, "NFS ACCESS cache timeout");

static int	nfs_prime_access_cache = 0;
SYSCTL_INT(_vfs_nfs, OID_AUTO, prime_access_cache, CTLFLAG_RW,
	   &nfs_prime_access_cache, 0,
	   "Prime NFS ACCESS cache when fetching attributes");

static int	newnfs_commit_on_close = 0;
SYSCTL_INT(_vfs_nfs, OID_AUTO, commit_on_close, CTLFLAG_RW,
    &newnfs_commit_on_close, 0, "write+commit on close, else only write");

static int	nfs_clean_pages_on_close = 1;
SYSCTL_INT(_vfs_nfs, OID_AUTO, clean_pages_on_close, CTLFLAG_RW,
	   &nfs_clean_pages_on_close, 0, "NFS clean dirty pages on close");

int newnfs_directio_enable = 0;
SYSCTL_INT(_vfs_nfs, OID_AUTO, nfs_directio_enable, CTLFLAG_RW,
	   &newnfs_directio_enable, 0, "Enable NFS directio");

int nfs_keep_dirty_on_error;
SYSCTL_INT(_vfs_nfs, OID_AUTO, nfs_keep_dirty_on_error, CTLFLAG_RW,
    &nfs_keep_dirty_on_error, 0, "Retry pageout if error returned");

/*
 * This sysctl allows other processes to mmap a file that has been opened
 * O_DIRECT by a process.  In general, having processes mmap the file while
 * Direct IO is in progress can lead to Data Inconsistencies.  But, we allow
 * this by default to prevent DoS attacks - to prevent a malicious user from
 * opening up files O_DIRECT preventing other users from mmap'ing these
 * files.  "Protected" environments where stricter consistency guarantees are
 * required can disable this knob.  The process that opened the file O_DIRECT
 * cannot mmap() the file, because mmap'ed IO on an O_DIRECT open() is not
 * meaningful.
 */
int newnfs_directio_allow_mmap = 1;
SYSCTL_INT(_vfs_nfs, OID_AUTO, nfs_directio_allow_mmap, CTLFLAG_RW,
	   &newnfs_directio_allow_mmap, 0, "Enable mmaped IO on file with O_DIRECT opens");

#define	NFSACCESS_ALL (NFSACCESS_READ | NFSACCESS_MODIFY		\
			 | NFSACCESS_EXTEND | NFSACCESS_EXECUTE	\
			 | NFSACCESS_DELETE | NFSACCESS_LOOKUP)

/*
 * SMP Locking Note :
 * The list of locks after the description of the lock is the ordering
 * of other locks acquired with the lock held.
 * np->n_mtx : Protects the fields in the nfsnode.
       VM Object Lock
       VI_MTX (acquired indirectly)
 * nmp->nm_mtx : Protects the fields in the nfsmount.
       rep->r_mtx
 * ncl_iod_mutex : Global lock, protects shared nfsiod state.
 * nfs_reqq_mtx : Global lock, protects the nfs_reqq list.
       nmp->nm_mtx
       rep->r_mtx
 * rep->r_mtx : Protects the fields in an nfsreq.
 */

static int
nfs34_access_otw(struct vnode *vp, int wmode, struct thread *td,
    struct ucred *cred, u_int32_t *retmode)
{
	int error = 0, attrflag, i, lrupos;
	u_int32_t rmode;
	struct nfsnode *np = VTONFS(vp);
	struct nfsvattr nfsva;

	error = nfsrpc_accessrpc(vp, wmode, cred, td, &nfsva, &attrflag,
	    &rmode, NULL);
	if (attrflag)
		(void) nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 0, 1);
	if (!error) {
		lrupos = 0;
		mtx_lock(&np->n_mtx);
		for (i = 0; i < NFS_ACCESSCACHESIZE; i++) {
			if (np->n_accesscache[i].uid == cred->cr_uid) {
				np->n_accesscache[i].mode = rmode;
				np->n_accesscache[i].stamp = time_second;
				break;
			}
			if (i > 0 && np->n_accesscache[i].stamp <
			    np->n_accesscache[lrupos].stamp)
				lrupos = i;
		}
		if (i == NFS_ACCESSCACHESIZE) {
			np->n_accesscache[lrupos].uid = cred->cr_uid;
			np->n_accesscache[lrupos].mode = rmode;
			np->n_accesscache[lrupos].stamp = time_second;
		}
		mtx_unlock(&np->n_mtx);
		if (retmode != NULL)
			*retmode = rmode;
		KDTRACE_NFS_ACCESSCACHE_LOAD_DONE(vp, cred->cr_uid, rmode, 0);
	} else if (NFS_ISV4(vp)) {
		error = nfscl_maperr(td, error, (uid_t)0, (gid_t)0);
	}
#ifdef KDTRACE_HOOKS
	if (error != 0)
		KDTRACE_NFS_ACCESSCACHE_LOAD_DONE(vp, cred->cr_uid, 0,
		    error);
#endif
	return (error);
}

/*
 * nfs access vnode op.
 * For nfs version 2, just return ok. File accesses may fail later.
 * For nfs version 3, use the access rpc to check accessibility. If file modes
 * are changed on the server, accesses might still fail later.
 */
static int
nfs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int error = 0, i, gotahit;
	u_int32_t mode, wmode, rmode;
	int v34 = NFS_ISV34(vp);
	struct nfsnode *np = VTONFS(vp);

	/*
	 * Disallow write attempts on filesystems mounted read-only;
	 * unless the file is a socket, fifo, or a block or character
	 * device resident on the filesystem.
	 */
	if ((ap->a_accmode & (VWRITE | VAPPEND | VWRITE_NAMED_ATTRS |
	    VDELETE_CHILD | VWRITE_ATTRIBUTES | VDELETE | VWRITE_ACL |
	    VWRITE_OWNER)) != 0 && (vp->v_mount->mnt_flag & MNT_RDONLY) != 0) {
		switch (vp->v_type) {
		case VREG:
		case VDIR:
		case VLNK:
			return (EROFS);
		default:
			break;
		}
	}
	/*
	 * For nfs v3 or v4, check to see if we have done this recently, and if
	 * so return our cached result instead of making an ACCESS call.
	 * If not, do an access rpc, otherwise you are stuck emulating
	 * ufs_access() locally using the vattr. This may not be correct,
	 * since the server may apply other access criteria such as
	 * client uid-->server uid mapping that we do not know about.
	 */
	if (v34) {
		if (ap->a_accmode & VREAD)
			mode = NFSACCESS_READ;
		else
			mode = 0;
		if (vp->v_type != VDIR) {
			if (ap->a_accmode & VWRITE)
				mode |= (NFSACCESS_MODIFY | NFSACCESS_EXTEND);
			if (ap->a_accmode & VAPPEND)
				mode |= NFSACCESS_EXTEND;
			if (ap->a_accmode & VEXEC)
				mode |= NFSACCESS_EXECUTE;
			if (ap->a_accmode & VDELETE)
				mode |= NFSACCESS_DELETE;
		} else {
			if (ap->a_accmode & VWRITE)
				mode |= (NFSACCESS_MODIFY | NFSACCESS_EXTEND);
			if (ap->a_accmode & VAPPEND)
				mode |= NFSACCESS_EXTEND;
			if (ap->a_accmode & VEXEC)
				mode |= NFSACCESS_LOOKUP;
			if (ap->a_accmode & VDELETE)
				mode |= NFSACCESS_DELETE;
			if (ap->a_accmode & VDELETE_CHILD)
				mode |= NFSACCESS_MODIFY;
		}
		/* XXX safety belt, only make blanket request if caching */
		if (nfsaccess_cache_timeout > 0) {
			wmode = NFSACCESS_READ | NFSACCESS_MODIFY |
				NFSACCESS_EXTEND | NFSACCESS_EXECUTE |
				NFSACCESS_DELETE | NFSACCESS_LOOKUP;
		} else {
			wmode = mode;
		}

		/*
		 * Does our cached result allow us to give a definite yes to
		 * this request?
		 */
		gotahit = 0;
		mtx_lock(&np->n_mtx);
		for (i = 0; i < NFS_ACCESSCACHESIZE; i++) {
			if (ap->a_cred->cr_uid == np->n_accesscache[i].uid) {
			    if (time_second < (np->n_accesscache[i].stamp
				+ nfsaccess_cache_timeout) &&
				(np->n_accesscache[i].mode & mode) == mode) {
				NFSINCRGLOBAL(nfsstatsv1.accesscache_hits);
				gotahit = 1;
			    }
			    break;
			}
		}
		mtx_unlock(&np->n_mtx);
#ifdef KDTRACE_HOOKS
		if (gotahit != 0)
			KDTRACE_NFS_ACCESSCACHE_GET_HIT(vp,
			    ap->a_cred->cr_uid, mode);
		else
			KDTRACE_NFS_ACCESSCACHE_GET_MISS(vp,
			    ap->a_cred->cr_uid, mode);
#endif
		if (gotahit == 0) {
			/*
			 * Either a no, or a don't know.  Go to the wire.
			 */
			NFSINCRGLOBAL(nfsstatsv1.accesscache_misses);
		        error = nfs34_access_otw(vp, wmode, ap->a_td,
			    ap->a_cred, &rmode);
			if (!error &&
			    (rmode & mode) != mode)
				error = EACCES;
		}
		return (error);
	} else {
		if ((error = nfsspec_access(ap)) != 0) {
			return (error);
		}
		/*
		 * Attempt to prevent a mapped root from accessing a file
		 * which it shouldn't.  We try to read a byte from the file
		 * if the user is root and the file is not zero length.
		 * After calling nfsspec_access, we should have the correct
		 * file size cached.
		 */
		mtx_lock(&np->n_mtx);
		if (ap->a_cred->cr_uid == 0 && (ap->a_accmode & VREAD)
		    && VTONFS(vp)->n_size > 0) {
			struct iovec aiov;
			struct uio auio;
			char buf[1];

			mtx_unlock(&np->n_mtx);
			aiov.iov_base = buf;
			aiov.iov_len = 1;
			auio.uio_iov = &aiov;
			auio.uio_iovcnt = 1;
			auio.uio_offset = 0;
			auio.uio_resid = 1;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_READ;
			auio.uio_td = ap->a_td;

			if (vp->v_type == VREG)
				error = ncl_readrpc(vp, &auio, ap->a_cred);
			else if (vp->v_type == VDIR) {
				char* bp;
				bp = malloc(NFS_DIRBLKSIZ, M_TEMP, M_WAITOK);
				aiov.iov_base = bp;
				aiov.iov_len = auio.uio_resid = NFS_DIRBLKSIZ;
				error = ncl_readdirrpc(vp, &auio, ap->a_cred,
				    ap->a_td);
				free(bp, M_TEMP);
			} else if (vp->v_type == VLNK)
				error = ncl_readlinkrpc(vp, &auio, ap->a_cred);
			else
				error = EACCES;
		} else
			mtx_unlock(&np->n_mtx);
		return (error);
	}
}


/*
 * nfs open vnode op
 * Check to see if the type is ok
 * and that deletion is not in progress.
 * For paged in text files, you will need to flush the page cache
 * if consistency is lost.
 */
/* ARGSUSED */
static int
nfs_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct vattr vattr;
	int error;
	int fmode = ap->a_mode;
	struct ucred *cred;

	if (vp->v_type != VREG && vp->v_type != VDIR && vp->v_type != VLNK)
		return (EOPNOTSUPP);

	/*
	 * For NFSv4, we need to do the Open Op before cache validation,
	 * so that we conform to RFC3530 Sec. 9.3.1.
	 */
	if (NFS_ISV4(vp)) {
		error = nfsrpc_open(vp, fmode, ap->a_cred, ap->a_td);
		if (error) {
			error = nfscl_maperr(ap->a_td, error, (uid_t)0,
			    (gid_t)0);
			return (error);
		}
	}

	/*
	 * Now, if this Open will be doing reading, re-validate/flush the
	 * cache, so that Close/Open coherency is maintained.
	 */
	mtx_lock(&np->n_mtx);
	if (np->n_flag & NMODIFIED) {
		mtx_unlock(&np->n_mtx);
		error = ncl_vinvalbuf(vp, V_SAVE, ap->a_td, 1);
		if (error == EINTR || error == EIO) {
			if (NFS_ISV4(vp))
				(void) nfsrpc_close(vp, 0, ap->a_td);
			return (error);
		}
		mtx_lock(&np->n_mtx);
		np->n_attrstamp = 0;
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp);
		if (vp->v_type == VDIR)
			np->n_direofoffset = 0;
		mtx_unlock(&np->n_mtx);
		error = VOP_GETATTR(vp, &vattr, ap->a_cred);
		if (error) {
			if (NFS_ISV4(vp))
				(void) nfsrpc_close(vp, 0, ap->a_td);
			return (error);
		}
		mtx_lock(&np->n_mtx);
		np->n_mtime = vattr.va_mtime;
		if (NFS_ISV4(vp))
			np->n_change = vattr.va_filerev;
	} else {
		mtx_unlock(&np->n_mtx);
		error = VOP_GETATTR(vp, &vattr, ap->a_cred);
		if (error) {
			if (NFS_ISV4(vp))
				(void) nfsrpc_close(vp, 0, ap->a_td);
			return (error);
		}
		mtx_lock(&np->n_mtx);
		if ((NFS_ISV4(vp) && np->n_change != vattr.va_filerev) ||
		    NFS_TIMESPEC_COMPARE(&np->n_mtime, &vattr.va_mtime)) {
			if (vp->v_type == VDIR)
				np->n_direofoffset = 0;
			mtx_unlock(&np->n_mtx);
			error = ncl_vinvalbuf(vp, V_SAVE, ap->a_td, 1);
			if (error == EINTR || error == EIO) {
				if (NFS_ISV4(vp))
					(void) nfsrpc_close(vp, 0, ap->a_td);
				return (error);
			}
			mtx_lock(&np->n_mtx);
			np->n_mtime = vattr.va_mtime;
			if (NFS_ISV4(vp))
				np->n_change = vattr.va_filerev;
		}
	}

	/*
	 * If the object has >= 1 O_DIRECT active opens, we disable caching.
	 */
	if (newnfs_directio_enable && (fmode & O_DIRECT) &&
	    (vp->v_type == VREG)) {
		if (np->n_directio_opens == 0) {
			mtx_unlock(&np->n_mtx);
			error = ncl_vinvalbuf(vp, V_SAVE, ap->a_td, 1);
			if (error) {
				if (NFS_ISV4(vp))
					(void) nfsrpc_close(vp, 0, ap->a_td);
				return (error);
			}
			mtx_lock(&np->n_mtx);
			np->n_flag |= NNONCACHE;
		}
		np->n_directio_opens++;
	}

	/* If opened for writing via NFSv4.1 or later, mark that for pNFS. */
	if (NFSHASPNFS(VFSTONFS(vp->v_mount)) && (fmode & FWRITE) != 0)
		np->n_flag |= NWRITEOPENED;

	/*
	 * If this is an open for writing, capture a reference to the
	 * credentials, so they can be used by ncl_putpages(). Using
	 * these write credentials is preferable to the credentials of
	 * whatever thread happens to be doing the VOP_PUTPAGES() since
	 * the write RPCs are less likely to fail with EACCES.
	 */
	if ((fmode & FWRITE) != 0) {
		cred = np->n_writecred;
		np->n_writecred = crhold(ap->a_cred);
	} else
		cred = NULL;
	mtx_unlock(&np->n_mtx);

	if (cred != NULL)
		crfree(cred);
	vnode_create_vobject(vp, vattr.va_size, ap->a_td);
	return (0);
}

/*
 * nfs close vnode op
 * What an NFS client should do upon close after writing is a debatable issue.
 * Most NFS clients push delayed writes to the server upon close, basically for
 * two reasons:
 * 1 - So that any write errors may be reported back to the client process
 *     doing the close system call. By far the two most likely errors are
 *     NFSERR_NOSPC and NFSERR_DQUOT to indicate space allocation failure.
 * 2 - To put a worst case upper bound on cache inconsistency between
 *     multiple clients for the file.
 * There is also a consistency problem for Version 2 of the protocol w.r.t.
 * not being able to tell if other clients are writing a file concurrently,
 * since there is no way of knowing if the changed modify time in the reply
 * is only due to the write for this client.
 * (NFS Version 3 provides weak cache consistency data in the reply that
 *  should be sufficient to detect and handle this case.)
 *
 * The current code does the following:
 * for NFS Version 2 - play it safe and flush/invalidate all dirty buffers
 * for NFS Version 3 - flush dirty buffers to the server but don't invalidate
 *                     or commit them (this satisfies 1 and 2 except for the
 *                     case where the server crashes after this close but
 *                     before the commit RPC, which is felt to be "good
 *                     enough". Changing the last argument to ncl_flush() to
 *                     a 1 would force a commit operation, if it is felt a
 *                     commit is necessary now.
 * for NFS Version 4 - flush the dirty buffers and commit them, if
 *		       nfscl_mustflush() says this is necessary.
 *                     It is necessary if there is no write delegation held,
 *                     in order to satisfy open/close coherency.
 *                     If the file isn't cached on local stable storage,
 *                     it may be necessary in order to detect "out of space"
 *                     errors from the server, if the write delegation
 *                     issued by the server doesn't allow the file to grow.
 */
/* ARGSUSED */
static int
nfs_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsvattr nfsva;
	struct ucred *cred;
	int error = 0, ret, localcred = 0;
	int fmode = ap->a_fflag;

	if (NFSCL_FORCEDISM(vp->v_mount))
		return (0);
	/*
	 * During shutdown, a_cred isn't valid, so just use root.
	 */
	if (ap->a_cred == NOCRED) {
		cred = newnfs_getcred();
		localcred = 1;
	} else {
		cred = ap->a_cred;
	}
	if (vp->v_type == VREG) {
	    /*
	     * Examine and clean dirty pages, regardless of NMODIFIED.
	     * This closes a major hole in close-to-open consistency.
	     * We want to push out all dirty pages (and buffers) on
	     * close, regardless of whether they were dirtied by
	     * mmap'ed writes or via write().
	     */
	    if (nfs_clean_pages_on_close && vp->v_object) {
		VM_OBJECT_WLOCK(vp->v_object);
		vm_object_page_clean(vp->v_object, 0, 0, 0);
		VM_OBJECT_WUNLOCK(vp->v_object);
	    }
	    mtx_lock(&np->n_mtx);
	    if (np->n_flag & NMODIFIED) {
		mtx_unlock(&np->n_mtx);
		if (NFS_ISV3(vp)) {
		    /*
		     * Under NFSv3 we have dirty buffers to dispose of.  We
		     * must flush them to the NFS server.  We have the option
		     * of waiting all the way through the commit rpc or just
		     * waiting for the initial write.  The default is to only
		     * wait through the initial write so the data is in the
		     * server's cache, which is roughly similar to the state
		     * a standard disk subsystem leaves the file in on close().
		     *
		     * We cannot clear the NMODIFIED bit in np->n_flag due to
		     * potential races with other processes, and certainly
		     * cannot clear it if we don't commit.
		     * These races occur when there is no longer the old
		     * traditional vnode locking implemented for Vnode Ops.
		     */
		    int cm = newnfs_commit_on_close ? 1 : 0;
		    error = ncl_flush(vp, MNT_WAIT, ap->a_td, cm, 0);
		    /* np->n_flag &= ~NMODIFIED; */
		} else if (NFS_ISV4(vp)) { 
			if (nfscl_mustflush(vp) != 0) {
				int cm = newnfs_commit_on_close ? 1 : 0;
				error = ncl_flush(vp, MNT_WAIT, ap->a_td,
				    cm, 0);
				/*
				 * as above w.r.t races when clearing
				 * NMODIFIED.
				 * np->n_flag &= ~NMODIFIED;
				 */
			}
		} else {
			error = ncl_vinvalbuf(vp, V_SAVE, ap->a_td, 1);
		}
		mtx_lock(&np->n_mtx);
	    }
 	    /* 
 	     * Invalidate the attribute cache in all cases.
 	     * An open is going to fetch fresh attrs any way, other procs
 	     * on this node that have file open will be forced to do an 
 	     * otw attr fetch, but this is safe.
	     * --> A user found that their RPC count dropped by 20% when
	     *     this was commented out and I can't see any requirement
	     *     for it, so I've disabled it when negative lookups are
	     *     enabled. (What does this have to do with negative lookup
	     *     caching? Well nothing, except it was reported by the
	     *     same user that needed negative lookup caching and I wanted
	     *     there to be a way to disable it to see if it
	     *     is the cause of some caching/coherency issue that might
	     *     crop up.)
 	     */
	    if (VFSTONFS(vp->v_mount)->nm_negnametimeo == 0) {
		    np->n_attrstamp = 0;
		    KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp);
	    }
	    if (np->n_flag & NWRITEERR) {
		np->n_flag &= ~NWRITEERR;
		error = np->n_error;
	    }
	    mtx_unlock(&np->n_mtx);
	}

	if (NFS_ISV4(vp)) {
		/*
		 * Get attributes so "change" is up to date.
		 */
		if (error == 0 && nfscl_mustflush(vp) != 0 &&
		    vp->v_type == VREG &&
		    (VFSTONFS(vp->v_mount)->nm_flag & NFSMNT_NOCTO) == 0) {
			ret = nfsrpc_getattr(vp, cred, ap->a_td, &nfsva,
			    NULL);
			if (!ret) {
				np->n_change = nfsva.na_filerev;
				(void) nfscl_loadattrcache(&vp, &nfsva, NULL,
				    NULL, 0, 0);
			}
		}

		/*
		 * and do the close.
		 */
		ret = nfsrpc_close(vp, 0, ap->a_td);
		if (!error && ret)
			error = ret;
		if (error)
			error = nfscl_maperr(ap->a_td, error, (uid_t)0,
			    (gid_t)0);
	}
	if (newnfs_directio_enable)
		KASSERT((np->n_directio_asyncwr == 0),
			("nfs_close: dirty unflushed (%d) directio buffers\n",
			 np->n_directio_asyncwr));
	if (newnfs_directio_enable && (fmode & O_DIRECT) && (vp->v_type == VREG)) {
		mtx_lock(&np->n_mtx);
		KASSERT((np->n_directio_opens > 0), 
			("nfs_close: unexpectedly value (0) of n_directio_opens\n"));
		np->n_directio_opens--;
		if (np->n_directio_opens == 0)
			np->n_flag &= ~NNONCACHE;
		mtx_unlock(&np->n_mtx);
	}
	if (localcred)
		NFSFREECRED(cred);
	return (error);
}

/*
 * nfs getattr call from vfs.
 */
static int
nfs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct thread *td = curthread;	/* XXX */
	struct nfsnode *np = VTONFS(vp);
	int error = 0;
	struct nfsvattr nfsva;
	struct vattr *vap = ap->a_vap;
	struct vattr vattr;

	/*
	 * Update local times for special files.
	 */
	mtx_lock(&np->n_mtx);
	if (np->n_flag & (NACC | NUPD))
		np->n_flag |= NCHG;
	mtx_unlock(&np->n_mtx);
	/*
	 * First look in the cache.
	 */
	if (ncl_getattrcache(vp, &vattr) == 0) {
		vap->va_type = vattr.va_type;
		vap->va_mode = vattr.va_mode;
		vap->va_nlink = vattr.va_nlink;
		vap->va_uid = vattr.va_uid;
		vap->va_gid = vattr.va_gid;
		vap->va_fsid = vattr.va_fsid;
		vap->va_fileid = vattr.va_fileid;
		vap->va_size = vattr.va_size;
		vap->va_blocksize = vattr.va_blocksize;
		vap->va_atime = vattr.va_atime;
		vap->va_mtime = vattr.va_mtime;
		vap->va_ctime = vattr.va_ctime;
		vap->va_gen = vattr.va_gen;
		vap->va_flags = vattr.va_flags;
		vap->va_rdev = vattr.va_rdev;
		vap->va_bytes = vattr.va_bytes;
		vap->va_filerev = vattr.va_filerev;
		/*
		 * Get the local modify time for the case of a write
		 * delegation.
		 */
		nfscl_deleggetmodtime(vp, &vap->va_mtime);
		return (0);
	}

	if (NFS_ISV34(vp) && nfs_prime_access_cache &&
	    nfsaccess_cache_timeout > 0) {
		NFSINCRGLOBAL(nfsstatsv1.accesscache_misses);
		nfs34_access_otw(vp, NFSACCESS_ALL, td, ap->a_cred, NULL);
		if (ncl_getattrcache(vp, ap->a_vap) == 0) {
			nfscl_deleggetmodtime(vp, &ap->a_vap->va_mtime);
			return (0);
		}
	}
	error = nfsrpc_getattr(vp, ap->a_cred, td, &nfsva, NULL);
	if (!error)
		error = nfscl_loadattrcache(&vp, &nfsva, vap, NULL, 0, 0);
	if (!error) {
		/*
		 * Get the local modify time for the case of a write
		 * delegation.
		 */
		nfscl_deleggetmodtime(vp, &vap->va_mtime);
	} else if (NFS_ISV4(vp)) {
		error = nfscl_maperr(td, error, (uid_t)0, (gid_t)0);
	}
	return (error);
}

/*
 * nfs setattr call.
 */
static int
nfs_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct thread *td = curthread;	/* XXX */
	struct vattr *vap = ap->a_vap;
	int error = 0;
	u_quad_t tsize;

#ifndef nolint
	tsize = (u_quad_t)0;
#endif

	/*
	 * Setting of flags and marking of atimes are not supported.
	 */
	if (vap->va_flags != VNOVAL)
		return (EOPNOTSUPP);

	/*
	 * Disallow write attempts if the filesystem is mounted read-only.
	 */
  	if ((vap->va_flags != VNOVAL || vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL || vap->va_atime.tv_sec != VNOVAL ||
	    vap->va_mtime.tv_sec != VNOVAL || vap->va_mode != (mode_t)VNOVAL) &&
	    (vp->v_mount->mnt_flag & MNT_RDONLY))
		return (EROFS);
	if (vap->va_size != VNOVAL) {
 		switch (vp->v_type) {
 		case VDIR:
 			return (EISDIR);
 		case VCHR:
 		case VBLK:
 		case VSOCK:
 		case VFIFO:
			if (vap->va_mtime.tv_sec == VNOVAL &&
			    vap->va_atime.tv_sec == VNOVAL &&
			    vap->va_mode == (mode_t)VNOVAL &&
			    vap->va_uid == (uid_t)VNOVAL &&
			    vap->va_gid == (gid_t)VNOVAL)
				return (0);		
 			vap->va_size = VNOVAL;
 			break;
 		default:
			/*
			 * Disallow write attempts if the filesystem is
			 * mounted read-only.
			 */
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			/*
			 *  We run vnode_pager_setsize() early (why?),
			 * we must set np->n_size now to avoid vinvalbuf
			 * V_SAVE races that might setsize a lower
			 * value.
			 */
			mtx_lock(&np->n_mtx);
			tsize = np->n_size;
			mtx_unlock(&np->n_mtx);
			error = ncl_meta_setsize(vp, ap->a_cred, td,
			    vap->va_size);
			mtx_lock(&np->n_mtx);
 			if (np->n_flag & NMODIFIED) {
			    tsize = np->n_size;
			    mtx_unlock(&np->n_mtx);
			    error = ncl_vinvalbuf(vp, vap->va_size == 0 ?
			        0 : V_SAVE, td, 1);
			    if (error != 0) {
				    vnode_pager_setsize(vp, tsize);
				    return (error);
			    }
			    /*
			     * Call nfscl_delegmodtime() to set the modify time
			     * locally, as required.
			     */
			    nfscl_delegmodtime(vp);
 			} else
			    mtx_unlock(&np->n_mtx);
			/*
			 * np->n_size has already been set to vap->va_size
			 * in ncl_meta_setsize(). We must set it again since
			 * nfs_loadattrcache() could be called through
			 * ncl_meta_setsize() and could modify np->n_size.
			 */
			mtx_lock(&np->n_mtx);
 			np->n_vattr.na_size = np->n_size = vap->va_size;
			mtx_unlock(&np->n_mtx);
  		}
  	} else {
		mtx_lock(&np->n_mtx);
		if ((vap->va_mtime.tv_sec != VNOVAL || vap->va_atime.tv_sec != VNOVAL) && 
		    (np->n_flag & NMODIFIED) && vp->v_type == VREG) {
			mtx_unlock(&np->n_mtx);
			error = ncl_vinvalbuf(vp, V_SAVE, td, 1);
			if (error == EINTR || error == EIO)
				return (error);
		} else
			mtx_unlock(&np->n_mtx);
	}
	error = nfs_setattrrpc(vp, vap, ap->a_cred, td);
	if (error && vap->va_size != VNOVAL) {
		mtx_lock(&np->n_mtx);
		np->n_size = np->n_vattr.na_size = tsize;
		vnode_pager_setsize(vp, tsize);
		mtx_unlock(&np->n_mtx);
	}
	return (error);
}

/*
 * Do an nfs setattr rpc.
 */
static int
nfs_setattrrpc(struct vnode *vp, struct vattr *vap, struct ucred *cred,
    struct thread *td)
{
	struct nfsnode *np = VTONFS(vp);
	int error, ret, attrflag, i;
	struct nfsvattr nfsva;

	if (NFS_ISV34(vp)) {
		mtx_lock(&np->n_mtx);
		for (i = 0; i < NFS_ACCESSCACHESIZE; i++)
			np->n_accesscache[i].stamp = 0;
		np->n_flag |= NDELEGMOD;
		mtx_unlock(&np->n_mtx);
		KDTRACE_NFS_ACCESSCACHE_FLUSH_DONE(vp);
	}
	error = nfsrpc_setattr(vp, vap, NULL, cred, td, &nfsva, &attrflag,
	    NULL);
	if (attrflag) {
		ret = nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 0, 1);
		if (ret && !error)
			error = ret;
	}
	if (error && NFS_ISV4(vp))
		error = nfscl_maperr(td, error, vap->va_uid, vap->va_gid);
	return (error);
}

/*
 * nfs lookup call, one step at a time...
 * First look in cache
 * If not found, unlock the directory nfsnode and do the rpc
 */
static int
nfs_lookup(struct vop_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct mount *mp = dvp->v_mount;
	int flags = cnp->cn_flags;
	struct vnode *newvp;
	struct nfsmount *nmp;
	struct nfsnode *np, *newnp;
	int error = 0, attrflag, dattrflag, ltype, ncticks;
	struct thread *td = cnp->cn_thread;
	struct nfsfh *nfhp;
	struct nfsvattr dnfsva, nfsva;
	struct vattr vattr;
	struct timespec nctime;
	
	*vpp = NULLVP;
	if ((flags & ISLASTCN) && (mp->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME))
		return (EROFS);
	if (dvp->v_type != VDIR)
		return (ENOTDIR);
	nmp = VFSTONFS(mp);
	np = VTONFS(dvp);

	/* For NFSv4, wait until any remove is done. */
	mtx_lock(&np->n_mtx);
	while (NFSHASNFSV4(nmp) && (np->n_flag & NREMOVEINPROG)) {
		np->n_flag |= NREMOVEWANT;
		(void) msleep((caddr_t)np, &np->n_mtx, PZERO, "nfslkup", 0);
	}
	mtx_unlock(&np->n_mtx);

	if ((error = VOP_ACCESS(dvp, VEXEC, cnp->cn_cred, td)) != 0)
		return (error);
	error = cache_lookup(dvp, vpp, cnp, &nctime, &ncticks);
	if (error > 0 && error != ENOENT)
		return (error);
	if (error == -1) {
		/*
		 * Lookups of "." are special and always return the
		 * current directory.  cache_lookup() already handles
		 * associated locking bookkeeping, etc.
		 */
		if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
			/* XXX: Is this really correct? */
			if (cnp->cn_nameiop != LOOKUP &&
			    (flags & ISLASTCN))
				cnp->cn_flags |= SAVENAME;
			return (0);
		}

		/*
		 * We only accept a positive hit in the cache if the
		 * change time of the file matches our cached copy.
		 * Otherwise, we discard the cache entry and fallback
		 * to doing a lookup RPC.  We also only trust cache
		 * entries for less than nm_nametimeo seconds.
		 *
		 * To better handle stale file handles and attributes,
		 * clear the attribute cache of this node if it is a
		 * leaf component, part of an open() call, and not
		 * locally modified before fetching the attributes.
		 * This should allow stale file handles to be detected
		 * here where we can fall back to a LOOKUP RPC to
		 * recover rather than having nfs_open() detect the
		 * stale file handle and failing open(2) with ESTALE.
		 */
		newvp = *vpp;
		newnp = VTONFS(newvp);
		if (!(nmp->nm_flag & NFSMNT_NOCTO) &&
		    (flags & (ISLASTCN | ISOPEN)) == (ISLASTCN | ISOPEN) &&
		    !(newnp->n_flag & NMODIFIED)) {
			mtx_lock(&newnp->n_mtx);
			newnp->n_attrstamp = 0;
			KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(newvp);
			mtx_unlock(&newnp->n_mtx);
		}
		if (nfscl_nodeleg(newvp, 0) == 0 ||
		    ((u_int)(ticks - ncticks) < (nmp->nm_nametimeo * hz) &&
		    VOP_GETATTR(newvp, &vattr, cnp->cn_cred) == 0 &&
		    timespeccmp(&vattr.va_ctime, &nctime, ==))) {
			NFSINCRGLOBAL(nfsstatsv1.lookupcache_hits);
			if (cnp->cn_nameiop != LOOKUP &&
			    (flags & ISLASTCN))
				cnp->cn_flags |= SAVENAME;
			return (0);
		}
		cache_purge(newvp);
		if (dvp != newvp)
			vput(newvp);
		else 
			vrele(newvp);
		*vpp = NULLVP;
	} else if (error == ENOENT) {
		if (dvp->v_iflag & VI_DOOMED)
			return (ENOENT);
		/*
		 * We only accept a negative hit in the cache if the
		 * modification time of the parent directory matches
		 * the cached copy in the name cache entry.
		 * Otherwise, we discard all of the negative cache
		 * entries for this directory.  We also only trust
		 * negative cache entries for up to nm_negnametimeo
		 * seconds.
		 */
		if ((u_int)(ticks - ncticks) < (nmp->nm_negnametimeo * hz) &&
		    VOP_GETATTR(dvp, &vattr, cnp->cn_cred) == 0 &&
		    timespeccmp(&vattr.va_mtime, &nctime, ==)) {
			NFSINCRGLOBAL(nfsstatsv1.lookupcache_hits);
			return (ENOENT);
		}
		cache_purge_negative(dvp);
	}

	error = 0;
	newvp = NULLVP;
	NFSINCRGLOBAL(nfsstatsv1.lookupcache_misses);
	error = nfsrpc_lookup(dvp, cnp->cn_nameptr, cnp->cn_namelen,
	    cnp->cn_cred, td, &dnfsva, &nfsva, &nfhp, &attrflag, &dattrflag,
	    NULL);
	if (dattrflag)
		(void) nfscl_loadattrcache(&dvp, &dnfsva, NULL, NULL, 0, 1);
	if (error) {
		if (newvp != NULLVP) {
			vput(newvp);
			*vpp = NULLVP;
		}

		if (error != ENOENT) {
			if (NFS_ISV4(dvp))
				error = nfscl_maperr(td, error, (uid_t)0,
				    (gid_t)0);
			return (error);
		}

		/* The requested file was not found. */
		if ((cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME) &&
		    (flags & ISLASTCN)) {
			/*
			 * XXX: UFS does a full VOP_ACCESS(dvp,
			 * VWRITE) here instead of just checking
			 * MNT_RDONLY.
			 */
			if (mp->mnt_flag & MNT_RDONLY)
				return (EROFS);
			cnp->cn_flags |= SAVENAME;
			return (EJUSTRETURN);
		}

		if ((cnp->cn_flags & MAKEENTRY) != 0 && dattrflag) {
			/*
			 * Cache the modification time of the parent
			 * directory from the post-op attributes in
			 * the name cache entry.  The negative cache
			 * entry will be ignored once the directory
			 * has changed.  Don't bother adding the entry
			 * if the directory has already changed.
			 */
			mtx_lock(&np->n_mtx);
			if (timespeccmp(&np->n_vattr.na_mtime,
			    &dnfsva.na_mtime, ==)) {
				mtx_unlock(&np->n_mtx);
				cache_enter_time(dvp, NULL, cnp,
				    &dnfsva.na_mtime, NULL);
			} else
				mtx_unlock(&np->n_mtx);
		}
		return (ENOENT);
	}

	/*
	 * Handle RENAME case...
	 */
	if (cnp->cn_nameiop == RENAME && (flags & ISLASTCN)) {
		if (NFS_CMPFH(np, nfhp->nfh_fh, nfhp->nfh_len)) {
			free(nfhp, M_NFSFH);
			return (EISDIR);
		}
		error = nfscl_nget(mp, dvp, nfhp, cnp, td, &np, NULL,
		    LK_EXCLUSIVE);
		if (error)
			return (error);
		newvp = NFSTOV(np);
		if (attrflag)
			(void) nfscl_loadattrcache(&newvp, &nfsva, NULL, NULL,
			    0, 1);
		*vpp = newvp;
		cnp->cn_flags |= SAVENAME;
		return (0);
	}

	if (flags & ISDOTDOT) {
		ltype = NFSVOPISLOCKED(dvp);
		error = vfs_busy(mp, MBF_NOWAIT);
		if (error != 0) {
			vfs_ref(mp);
			NFSVOPUNLOCK(dvp, 0);
			error = vfs_busy(mp, 0);
			NFSVOPLOCK(dvp, ltype | LK_RETRY);
			vfs_rel(mp);
			if (error == 0 && (dvp->v_iflag & VI_DOOMED)) {
				vfs_unbusy(mp);
				error = ENOENT;
			}
			if (error != 0)
				return (error);
		}
		NFSVOPUNLOCK(dvp, 0);
		error = nfscl_nget(mp, dvp, nfhp, cnp, td, &np, NULL,
		    cnp->cn_lkflags);
		if (error == 0)
			newvp = NFSTOV(np);
		vfs_unbusy(mp);
		if (newvp != dvp)
			NFSVOPLOCK(dvp, ltype | LK_RETRY);
		if (dvp->v_iflag & VI_DOOMED) {
			if (error == 0) {
				if (newvp == dvp)
					vrele(newvp);
				else
					vput(newvp);
			}
			error = ENOENT;
		}
		if (error != 0)
			return (error);
		if (attrflag)
			(void) nfscl_loadattrcache(&newvp, &nfsva, NULL, NULL,
			    0, 1);
	} else if (NFS_CMPFH(np, nfhp->nfh_fh, nfhp->nfh_len)) {
		free(nfhp, M_NFSFH);
		VREF(dvp);
		newvp = dvp;
		if (attrflag)
			(void) nfscl_loadattrcache(&newvp, &nfsva, NULL, NULL,
			    0, 1);
	} else {
		error = nfscl_nget(mp, dvp, nfhp, cnp, td, &np, NULL,
		    cnp->cn_lkflags);
		if (error)
			return (error);
		newvp = NFSTOV(np);
		if (attrflag)
			(void) nfscl_loadattrcache(&newvp, &nfsva, NULL, NULL,
			    0, 1);
		else if ((flags & (ISLASTCN | ISOPEN)) == (ISLASTCN | ISOPEN) &&
		    !(np->n_flag & NMODIFIED)) {			
			/*
			 * Flush the attribute cache when opening a
			 * leaf node to ensure that fresh attributes
			 * are fetched in nfs_open() since we did not
			 * fetch attributes from the LOOKUP reply.
			 */
			mtx_lock(&np->n_mtx);
			np->n_attrstamp = 0;
			KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(newvp);
			mtx_unlock(&np->n_mtx);
		}
	}
	if (cnp->cn_nameiop != LOOKUP && (flags & ISLASTCN))
		cnp->cn_flags |= SAVENAME;
	if ((cnp->cn_flags & MAKEENTRY) &&
	    (cnp->cn_nameiop != DELETE || !(flags & ISLASTCN)) &&
	    attrflag != 0 && (newvp->v_type != VDIR || dattrflag != 0))
		cache_enter_time(dvp, newvp, cnp, &nfsva.na_ctime,
		    newvp->v_type != VDIR ? NULL : &dnfsva.na_ctime);
	*vpp = newvp;
	return (0);
}

/*
 * nfs read call.
 * Just call ncl_bioread() to do the work.
 */
static int
nfs_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;

	switch (vp->v_type) {
	case VREG:
		return (ncl_bioread(vp, ap->a_uio, ap->a_ioflag, ap->a_cred));
	case VDIR:
		return (EISDIR);
	default:
		return (EOPNOTSUPP);
	}
}

/*
 * nfs readlink call
 */
static int
nfs_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;

	if (vp->v_type != VLNK)
		return (EINVAL);
	return (ncl_bioread(vp, ap->a_uio, 0, ap->a_cred));
}

/*
 * Do a readlink rpc.
 * Called by ncl_doio() from below the buffer cache.
 */
int
ncl_readlinkrpc(struct vnode *vp, struct uio *uiop, struct ucred *cred)
{
	int error, ret, attrflag;
	struct nfsvattr nfsva;

	error = nfsrpc_readlink(vp, uiop, cred, uiop->uio_td, &nfsva,
	    &attrflag, NULL);
	if (attrflag) {
		ret = nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 0, 1);
		if (ret && !error)
			error = ret;
	}
	if (error && NFS_ISV4(vp))
		error = nfscl_maperr(uiop->uio_td, error, (uid_t)0, (gid_t)0);
	return (error);
}

/*
 * nfs read rpc call
 * Ditto above
 */
int
ncl_readrpc(struct vnode *vp, struct uio *uiop, struct ucred *cred)
{
	int error, ret, attrflag;
	struct nfsvattr nfsva;
	struct nfsmount *nmp;

	nmp = VFSTONFS(vnode_mount(vp));
	error = EIO;
	attrflag = 0;
	if (NFSHASPNFS(nmp))
		error = nfscl_doiods(vp, uiop, NULL, NULL,
		    NFSV4OPEN_ACCESSREAD, 0, cred, uiop->uio_td);
	NFSCL_DEBUG(4, "readrpc: aft doiods=%d\n", error);
	if (error != 0)
		error = nfsrpc_read(vp, uiop, cred, uiop->uio_td, &nfsva,
		    &attrflag, NULL);
	if (attrflag) {
		ret = nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 0, 1);
		if (ret && !error)
			error = ret;
	}
	if (error && NFS_ISV4(vp))
		error = nfscl_maperr(uiop->uio_td, error, (uid_t)0, (gid_t)0);
	return (error);
}

/*
 * nfs write call
 */
int
ncl_writerpc(struct vnode *vp, struct uio *uiop, struct ucred *cred,
    int *iomode, int *must_commit, int called_from_strategy)
{
	struct nfsvattr nfsva;
	int error, attrflag, ret;
	struct nfsmount *nmp;

	nmp = VFSTONFS(vnode_mount(vp));
	error = EIO;
	attrflag = 0;
	if (NFSHASPNFS(nmp))
		error = nfscl_doiods(vp, uiop, iomode, must_commit,
		    NFSV4OPEN_ACCESSWRITE, 0, cred, uiop->uio_td);
	NFSCL_DEBUG(4, "writerpc: aft doiods=%d\n", error);
	if (error != 0)
		error = nfsrpc_write(vp, uiop, iomode, must_commit, cred,
		    uiop->uio_td, &nfsva, &attrflag, NULL,
		    called_from_strategy);
	if (attrflag) {
		if (VTONFS(vp)->n_flag & ND_NFSV4)
			ret = nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 1,
			    1);
		else
			ret = nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 0,
			    1);
		if (ret && !error)
			error = ret;
	}
	if (DOINGASYNC(vp))
		*iomode = NFSWRITE_FILESYNC;
	if (error && NFS_ISV4(vp))
		error = nfscl_maperr(uiop->uio_td, error, (uid_t)0, (gid_t)0);
	return (error);
}

/*
 * nfs mknod rpc
 * For NFS v2 this is a kludge. Use a create rpc but with the IFMT bits of the
 * mode set to specify the file type and the size field for rdev.
 */
static int
nfs_mknodrpc(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp,
    struct vattr *vap)
{
	struct nfsvattr nfsva, dnfsva;
	struct vnode *newvp = NULL;
	struct nfsnode *np = NULL, *dnp;
	struct nfsfh *nfhp;
	struct vattr vattr;
	int error = 0, attrflag, dattrflag;
	u_int32_t rdev;

	if (vap->va_type == VCHR || vap->va_type == VBLK)
		rdev = vap->va_rdev;
	else if (vap->va_type == VFIFO || vap->va_type == VSOCK)
		rdev = 0xffffffff;
	else
		return (EOPNOTSUPP);
	if ((error = VOP_GETATTR(dvp, &vattr, cnp->cn_cred)))
		return (error);
	error = nfsrpc_mknod(dvp, cnp->cn_nameptr, cnp->cn_namelen, vap,
	    rdev, vap->va_type, cnp->cn_cred, cnp->cn_thread, &dnfsva,
	    &nfsva, &nfhp, &attrflag, &dattrflag, NULL);
	if (!error) {
		if (!nfhp)
			(void) nfsrpc_lookup(dvp, cnp->cn_nameptr,
			    cnp->cn_namelen, cnp->cn_cred, cnp->cn_thread,
			    &dnfsva, &nfsva, &nfhp, &attrflag, &dattrflag,
			    NULL);
		if (nfhp)
			error = nfscl_nget(dvp->v_mount, dvp, nfhp, cnp,
			    cnp->cn_thread, &np, NULL, LK_EXCLUSIVE);
	}
	if (dattrflag)
		(void) nfscl_loadattrcache(&dvp, &dnfsva, NULL, NULL, 0, 1);
	if (!error) {
		newvp = NFSTOV(np);
		if (attrflag != 0) {
			error = nfscl_loadattrcache(&newvp, &nfsva, NULL, NULL,
			    0, 1);
			if (error != 0)
				vput(newvp);
		}
	}
	if (!error) {
		*vpp = newvp;
	} else if (NFS_ISV4(dvp)) {
		error = nfscl_maperr(cnp->cn_thread, error, vap->va_uid,
		    vap->va_gid);
	}
	dnp = VTONFS(dvp);
	mtx_lock(&dnp->n_mtx);
	dnp->n_flag |= NMODIFIED;
	if (!dattrflag) {
		dnp->n_attrstamp = 0;
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(dvp);
	}
	mtx_unlock(&dnp->n_mtx);
	return (error);
}

/*
 * nfs mknod vop
 * just call nfs_mknodrpc() to do the work.
 */
/* ARGSUSED */
static int
nfs_mknod(struct vop_mknod_args *ap)
{
	return (nfs_mknodrpc(ap->a_dvp, ap->a_vpp, ap->a_cnp, ap->a_vap));
}

static struct mtx nfs_cverf_mtx;
MTX_SYSINIT(nfs_cverf_mtx, &nfs_cverf_mtx, "NFS create verifier mutex",
    MTX_DEF);

static nfsquad_t
nfs_get_cverf(void)
{
	static nfsquad_t cverf;
	nfsquad_t ret;
	static int cverf_initialized = 0;

	mtx_lock(&nfs_cverf_mtx);
	if (cverf_initialized == 0) {
		cverf.lval[0] = arc4random();
		cverf.lval[1] = arc4random();
		cverf_initialized = 1;
	} else
		cverf.qval++;
	ret = cverf;
	mtx_unlock(&nfs_cverf_mtx);

	return (ret);
}

/*
 * nfs file create call
 */
static int
nfs_create(struct vop_create_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct componentname *cnp = ap->a_cnp;
	struct nfsnode *np = NULL, *dnp;
	struct vnode *newvp = NULL;
	struct nfsmount *nmp;
	struct nfsvattr dnfsva, nfsva;
	struct nfsfh *nfhp;
	nfsquad_t cverf;
	int error = 0, attrflag, dattrflag, fmode = 0;
	struct vattr vattr;

	/*
	 * Oops, not for me..
	 */
	if (vap->va_type == VSOCK)
		return (nfs_mknodrpc(dvp, ap->a_vpp, cnp, vap));

	if ((error = VOP_GETATTR(dvp, &vattr, cnp->cn_cred)))
		return (error);
	if (vap->va_vaflags & VA_EXCLUSIVE)
		fmode |= O_EXCL;
	dnp = VTONFS(dvp);
	nmp = VFSTONFS(vnode_mount(dvp));
again:
	/* For NFSv4, wait until any remove is done. */
	mtx_lock(&dnp->n_mtx);
	while (NFSHASNFSV4(nmp) && (dnp->n_flag & NREMOVEINPROG)) {
		dnp->n_flag |= NREMOVEWANT;
		(void) msleep((caddr_t)dnp, &dnp->n_mtx, PZERO, "nfscrt", 0);
	}
	mtx_unlock(&dnp->n_mtx);

	cverf = nfs_get_cverf();
	error = nfsrpc_create(dvp, cnp->cn_nameptr, cnp->cn_namelen,
	    vap, cverf, fmode, cnp->cn_cred, cnp->cn_thread, &dnfsva, &nfsva,
	    &nfhp, &attrflag, &dattrflag, NULL);
	if (!error) {
		if (nfhp == NULL)
			(void) nfsrpc_lookup(dvp, cnp->cn_nameptr,
			    cnp->cn_namelen, cnp->cn_cred, cnp->cn_thread,
			    &dnfsva, &nfsva, &nfhp, &attrflag, &dattrflag,
			    NULL);
		if (nfhp != NULL)
			error = nfscl_nget(dvp->v_mount, dvp, nfhp, cnp,
			    cnp->cn_thread, &np, NULL, LK_EXCLUSIVE);
	}
	if (dattrflag)
		(void) nfscl_loadattrcache(&dvp, &dnfsva, NULL, NULL, 0, 1);
	if (!error) {
		newvp = NFSTOV(np);
		if (attrflag == 0)
			error = nfsrpc_getattr(newvp, cnp->cn_cred,
			    cnp->cn_thread, &nfsva, NULL);
		if (error == 0)
			error = nfscl_loadattrcache(&newvp, &nfsva, NULL, NULL,
			    0, 1);
	}
	if (error) {
		if (newvp != NULL) {
			vput(newvp);
			newvp = NULL;
		}
		if (NFS_ISV34(dvp) && (fmode & O_EXCL) &&
		    error == NFSERR_NOTSUPP) {
			fmode &= ~O_EXCL;
			goto again;
		}
	} else if (NFS_ISV34(dvp) && (fmode & O_EXCL)) {
		if (nfscl_checksattr(vap, &nfsva)) {
			error = nfsrpc_setattr(newvp, vap, NULL, cnp->cn_cred,
			    cnp->cn_thread, &nfsva, &attrflag, NULL);
			if (error && (vap->va_uid != (uid_t)VNOVAL ||
			    vap->va_gid != (gid_t)VNOVAL)) {
				/* try again without setting uid/gid */
				vap->va_uid = (uid_t)VNOVAL;
				vap->va_gid = (uid_t)VNOVAL;
				error = nfsrpc_setattr(newvp, vap, NULL, 
				    cnp->cn_cred, cnp->cn_thread, &nfsva,
				    &attrflag, NULL);
			}
			if (attrflag)
				(void) nfscl_loadattrcache(&newvp, &nfsva, NULL,
				    NULL, 0, 1);
			if (error != 0)
				vput(newvp);
		}
	}
	if (!error) {
		if ((cnp->cn_flags & MAKEENTRY) && attrflag)
			cache_enter_time(dvp, newvp, cnp, &nfsva.na_ctime,
			    NULL);
		*ap->a_vpp = newvp;
	} else if (NFS_ISV4(dvp)) {
		error = nfscl_maperr(cnp->cn_thread, error, vap->va_uid,
		    vap->va_gid);
	}
	mtx_lock(&dnp->n_mtx);
	dnp->n_flag |= NMODIFIED;
	if (!dattrflag) {
		dnp->n_attrstamp = 0;
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(dvp);
	}
	mtx_unlock(&dnp->n_mtx);
	return (error);
}

/*
 * nfs file remove call
 * To try and make nfs semantics closer to ufs semantics, a file that has
 * other processes using the vnode is renamed instead of removed and then
 * removed later on the last close.
 * - If v_usecount > 1
 *	  If a rename is not already in the works
 *	     call nfs_sillyrename() to set it up
 *     else
 *	  do the remove rpc
 */
static int
nfs_remove(struct vop_remove_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct nfsnode *np = VTONFS(vp);
	int error = 0;
	struct vattr vattr;

	KASSERT((cnp->cn_flags & HASBUF) != 0, ("nfs_remove: no name"));
	KASSERT(vrefcnt(vp) > 0, ("nfs_remove: bad v_usecount"));
	if (vp->v_type == VDIR)
		error = EPERM;
	else if (vrefcnt(vp) == 1 || (np->n_sillyrename &&
	    VOP_GETATTR(vp, &vattr, cnp->cn_cred) == 0 &&
	    vattr.va_nlink > 1)) {
		/*
		 * Purge the name cache so that the chance of a lookup for
		 * the name succeeding while the remove is in progress is
		 * minimized. Without node locking it can still happen, such
		 * that an I/O op returns ESTALE, but since you get this if
		 * another host removes the file..
		 */
		cache_purge(vp);
		/*
		 * throw away biocache buffers, mainly to avoid
		 * unnecessary delayed writes later.
		 */
		error = ncl_vinvalbuf(vp, 0, cnp->cn_thread, 1);
		if (error != EINTR && error != EIO)
			/* Do the rpc */
			error = nfs_removerpc(dvp, vp, cnp->cn_nameptr,
			    cnp->cn_namelen, cnp->cn_cred, cnp->cn_thread);
		/*
		 * Kludge City: If the first reply to the remove rpc is lost..
		 *   the reply to the retransmitted request will be ENOENT
		 *   since the file was in fact removed
		 *   Therefore, we cheat and return success.
		 */
		if (error == ENOENT)
			error = 0;
	} else if (!np->n_sillyrename)
		error = nfs_sillyrename(dvp, vp, cnp);
	mtx_lock(&np->n_mtx);
	np->n_attrstamp = 0;
	mtx_unlock(&np->n_mtx);
	KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp);
	return (error);
}

/*
 * nfs file remove rpc called from nfs_inactive
 */
int
ncl_removeit(struct sillyrename *sp, struct vnode *vp)
{
	/*
	 * Make sure that the directory vnode is still valid.
	 * XXX we should lock sp->s_dvp here.
	 */
	if (sp->s_dvp->v_type == VBAD)
		return (0);
	return (nfs_removerpc(sp->s_dvp, vp, sp->s_name, sp->s_namlen,
	    sp->s_cred, NULL));
}

/*
 * Nfs remove rpc, called from nfs_remove() and ncl_removeit().
 */
static int
nfs_removerpc(struct vnode *dvp, struct vnode *vp, char *name,
    int namelen, struct ucred *cred, struct thread *td)
{
	struct nfsvattr dnfsva;
	struct nfsnode *dnp = VTONFS(dvp);
	int error = 0, dattrflag;

	mtx_lock(&dnp->n_mtx);
	dnp->n_flag |= NREMOVEINPROG;
	mtx_unlock(&dnp->n_mtx);
	error = nfsrpc_remove(dvp, name, namelen, vp, cred, td, &dnfsva,
	    &dattrflag, NULL);
	mtx_lock(&dnp->n_mtx);
	if ((dnp->n_flag & NREMOVEWANT)) {
		dnp->n_flag &= ~(NREMOVEWANT | NREMOVEINPROG);
		mtx_unlock(&dnp->n_mtx);
		wakeup((caddr_t)dnp);
	} else {
		dnp->n_flag &= ~NREMOVEINPROG;
		mtx_unlock(&dnp->n_mtx);
	}
	if (dattrflag)
		(void) nfscl_loadattrcache(&dvp, &dnfsva, NULL, NULL, 0, 1);
	mtx_lock(&dnp->n_mtx);
	dnp->n_flag |= NMODIFIED;
	if (!dattrflag) {
		dnp->n_attrstamp = 0;
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(dvp);
	}
	mtx_unlock(&dnp->n_mtx);
	if (error && NFS_ISV4(dvp))
		error = nfscl_maperr(td, error, (uid_t)0, (gid_t)0);
	return (error);
}

/*
 * nfs file rename call
 */
static int
nfs_rename(struct vop_rename_args *ap)
{
	struct vnode *fvp = ap->a_fvp;
	struct vnode *tvp = ap->a_tvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct nfsnode *fnp = VTONFS(ap->a_fvp);
	struct nfsnode *tdnp = VTONFS(ap->a_tdvp);
	struct nfsv4node *newv4 = NULL;
	int error;

	KASSERT((tcnp->cn_flags & HASBUF) != 0 &&
	    (fcnp->cn_flags & HASBUF) != 0, ("nfs_rename: no name"));
	/* Check for cross-device rename */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
		goto out;
	}

	if (fvp == tvp) {
		printf("nfs_rename: fvp == tvp (can't happen)\n");
		error = 0;
		goto out;
	}
	if ((error = NFSVOPLOCK(fvp, LK_EXCLUSIVE)) != 0)
		goto out;

	/*
	 * We have to flush B_DELWRI data prior to renaming
	 * the file.  If we don't, the delayed-write buffers
	 * can be flushed out later after the file has gone stale
	 * under NFSV3.  NFSV2 does not have this problem because
	 * ( as far as I can tell ) it flushes dirty buffers more
	 * often.
	 * 
	 * Skip the rename operation if the fsync fails, this can happen
	 * due to the server's volume being full, when we pushed out data
	 * that was written back to our cache earlier. Not checking for
	 * this condition can result in potential (silent) data loss.
	 */
	error = VOP_FSYNC(fvp, MNT_WAIT, fcnp->cn_thread);
	NFSVOPUNLOCK(fvp, 0);
	if (!error && tvp)
		error = VOP_FSYNC(tvp, MNT_WAIT, tcnp->cn_thread);
	if (error)
		goto out;

	/*
	 * If the tvp exists and is in use, sillyrename it before doing the
	 * rename of the new file over it.
	 * XXX Can't sillyrename a directory.
	 */
	if (tvp && vrefcnt(tvp) > 1 && !VTONFS(tvp)->n_sillyrename &&
		tvp->v_type != VDIR && !nfs_sillyrename(tdvp, tvp, tcnp)) {
		vput(tvp);
		tvp = NULL;
	}

	error = nfs_renamerpc(fdvp, fvp, fcnp->cn_nameptr, fcnp->cn_namelen,
	    tdvp, tvp, tcnp->cn_nameptr, tcnp->cn_namelen, tcnp->cn_cred,
	    tcnp->cn_thread);

	if (error == 0 && NFS_ISV4(tdvp)) {
		/*
		 * For NFSv4, check to see if it is the same name and
		 * replace the name, if it is different.
		 */
		newv4 = malloc(
		    sizeof (struct nfsv4node) +
		    tdnp->n_fhp->nfh_len + tcnp->cn_namelen - 1,
		    M_NFSV4NODE, M_WAITOK);
		mtx_lock(&tdnp->n_mtx);
		mtx_lock(&fnp->n_mtx);
		if (fnp->n_v4 != NULL && fvp->v_type == VREG &&
		    (fnp->n_v4->n4_namelen != tcnp->cn_namelen ||
		      NFSBCMP(tcnp->cn_nameptr, NFS4NODENAME(fnp->n_v4),
		      tcnp->cn_namelen) ||
		      tdnp->n_fhp->nfh_len != fnp->n_v4->n4_fhlen ||
		      NFSBCMP(tdnp->n_fhp->nfh_fh, fnp->n_v4->n4_data,
			tdnp->n_fhp->nfh_len))) {
#ifdef notdef
{ char nnn[100]; int nnnl;
nnnl = (tcnp->cn_namelen < 100) ? tcnp->cn_namelen : 99;
bcopy(tcnp->cn_nameptr, nnn, nnnl);
nnn[nnnl] = '\0';
printf("ren replace=%s\n",nnn);
}
#endif
			free(fnp->n_v4, M_NFSV4NODE);
			fnp->n_v4 = newv4;
			newv4 = NULL;
			fnp->n_v4->n4_fhlen = tdnp->n_fhp->nfh_len;
			fnp->n_v4->n4_namelen = tcnp->cn_namelen;
			NFSBCOPY(tdnp->n_fhp->nfh_fh, fnp->n_v4->n4_data,
			    tdnp->n_fhp->nfh_len);
			NFSBCOPY(tcnp->cn_nameptr,
			    NFS4NODENAME(fnp->n_v4), tcnp->cn_namelen);
		}
		mtx_unlock(&tdnp->n_mtx);
		mtx_unlock(&fnp->n_mtx);
		if (newv4 != NULL)
			free(newv4, M_NFSV4NODE);
	}

	if (fvp->v_type == VDIR) {
		if (tvp != NULL && tvp->v_type == VDIR)
			cache_purge(tdvp);
		cache_purge(fdvp);
	}

out:
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	if (tvp)
		vput(tvp);
	vrele(fdvp);
	vrele(fvp);
	/*
	 * Kludge: Map ENOENT => 0 assuming that it is a reply to a retry.
	 */
	if (error == ENOENT)
		error = 0;
	return (error);
}

/*
 * nfs file rename rpc called from nfs_remove() above
 */
static int
nfs_renameit(struct vnode *sdvp, struct vnode *svp, struct componentname *scnp,
    struct sillyrename *sp)
{

	return (nfs_renamerpc(sdvp, svp, scnp->cn_nameptr, scnp->cn_namelen,
	    sdvp, NULL, sp->s_name, sp->s_namlen, scnp->cn_cred,
	    scnp->cn_thread));
}

/*
 * Do an nfs rename rpc. Called from nfs_rename() and nfs_renameit().
 */
static int
nfs_renamerpc(struct vnode *fdvp, struct vnode *fvp, char *fnameptr,
    int fnamelen, struct vnode *tdvp, struct vnode *tvp, char *tnameptr,
    int tnamelen, struct ucred *cred, struct thread *td)
{
	struct nfsvattr fnfsva, tnfsva;
	struct nfsnode *fdnp = VTONFS(fdvp);
	struct nfsnode *tdnp = VTONFS(tdvp);
	int error = 0, fattrflag, tattrflag;

	error = nfsrpc_rename(fdvp, fvp, fnameptr, fnamelen, tdvp, tvp,
	    tnameptr, tnamelen, cred, td, &fnfsva, &tnfsva, &fattrflag,
	    &tattrflag, NULL, NULL);
	mtx_lock(&fdnp->n_mtx);
	fdnp->n_flag |= NMODIFIED;
	if (fattrflag != 0) {
		mtx_unlock(&fdnp->n_mtx);
		(void) nfscl_loadattrcache(&fdvp, &fnfsva, NULL, NULL, 0, 1);
	} else {
		fdnp->n_attrstamp = 0;
		mtx_unlock(&fdnp->n_mtx);
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(fdvp);
	}
	mtx_lock(&tdnp->n_mtx);
	tdnp->n_flag |= NMODIFIED;
	if (tattrflag != 0) {
		mtx_unlock(&tdnp->n_mtx);
		(void) nfscl_loadattrcache(&tdvp, &tnfsva, NULL, NULL, 0, 1);
	} else {
		tdnp->n_attrstamp = 0;
		mtx_unlock(&tdnp->n_mtx);
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(tdvp);
	}
	if (error && NFS_ISV4(fdvp))
		error = nfscl_maperr(td, error, (uid_t)0, (gid_t)0);
	return (error);
}

/*
 * nfs hard link create call
 */
static int
nfs_link(struct vop_link_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *cnp = ap->a_cnp;
	struct nfsnode *np, *tdnp;
	struct nfsvattr nfsva, dnfsva;
	int error = 0, attrflag, dattrflag;

	/*
	 * Push all writes to the server, so that the attribute cache
	 * doesn't get "out of sync" with the server.
	 * XXX There should be a better way!
	 */
	VOP_FSYNC(vp, MNT_WAIT, cnp->cn_thread);

	error = nfsrpc_link(tdvp, vp, cnp->cn_nameptr, cnp->cn_namelen,
	    cnp->cn_cred, cnp->cn_thread, &dnfsva, &nfsva, &attrflag,
	    &dattrflag, NULL);
	tdnp = VTONFS(tdvp);
	mtx_lock(&tdnp->n_mtx);
	tdnp->n_flag |= NMODIFIED;
	if (dattrflag != 0) {
		mtx_unlock(&tdnp->n_mtx);
		(void) nfscl_loadattrcache(&tdvp, &dnfsva, NULL, NULL, 0, 1);
	} else {
		tdnp->n_attrstamp = 0;
		mtx_unlock(&tdnp->n_mtx);
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(tdvp);
	}
	if (attrflag)
		(void) nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 0, 1);
	else {
		np = VTONFS(vp);
		mtx_lock(&np->n_mtx);
		np->n_attrstamp = 0;
		mtx_unlock(&np->n_mtx);
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp);
	}
	/*
	 * If negative lookup caching is enabled, I might as well
	 * add an entry for this node. Not necessary for correctness,
	 * but if negative caching is enabled, then the system
	 * must care about lookup caching hit rate, so...
	 */
	if (VFSTONFS(vp->v_mount)->nm_negnametimeo != 0 &&
	    (cnp->cn_flags & MAKEENTRY) && attrflag != 0 && error == 0) {
		cache_enter_time(tdvp, vp, cnp, &nfsva.na_ctime, NULL);
	}
	if (error && NFS_ISV4(vp))
		error = nfscl_maperr(cnp->cn_thread, error, (uid_t)0,
		    (gid_t)0);
	return (error);
}

/*
 * nfs symbolic link create call
 */
static int
nfs_symlink(struct vop_symlink_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct componentname *cnp = ap->a_cnp;
	struct nfsvattr nfsva, dnfsva;
	struct nfsfh *nfhp;
	struct nfsnode *np = NULL, *dnp;
	struct vnode *newvp = NULL;
	int error = 0, attrflag, dattrflag, ret;

	vap->va_type = VLNK;
	error = nfsrpc_symlink(dvp, cnp->cn_nameptr, cnp->cn_namelen,
	    ap->a_target, vap, cnp->cn_cred, cnp->cn_thread, &dnfsva,
	    &nfsva, &nfhp, &attrflag, &dattrflag, NULL);
	if (nfhp) {
		ret = nfscl_nget(dvp->v_mount, dvp, nfhp, cnp, cnp->cn_thread,
		    &np, NULL, LK_EXCLUSIVE);
		if (!ret)
			newvp = NFSTOV(np);
		else if (!error)
			error = ret;
	}
	if (newvp != NULL) {
		if (attrflag)
			(void) nfscl_loadattrcache(&newvp, &nfsva, NULL, NULL,
			    0, 1);
	} else if (!error) {
		/*
		 * If we do not have an error and we could not extract the
		 * newvp from the response due to the request being NFSv2, we
		 * have to do a lookup in order to obtain a newvp to return.
		 */
		error = nfs_lookitup(dvp, cnp->cn_nameptr, cnp->cn_namelen,
		    cnp->cn_cred, cnp->cn_thread, &np);
		if (!error)
			newvp = NFSTOV(np);
	}
	if (error) {
		if (newvp)
			vput(newvp);
		if (NFS_ISV4(dvp))
			error = nfscl_maperr(cnp->cn_thread, error,
			    vap->va_uid, vap->va_gid);
	} else {
		*ap->a_vpp = newvp;
	}

	dnp = VTONFS(dvp);
	mtx_lock(&dnp->n_mtx);
	dnp->n_flag |= NMODIFIED;
	if (dattrflag != 0) {
		mtx_unlock(&dnp->n_mtx);
		(void) nfscl_loadattrcache(&dvp, &dnfsva, NULL, NULL, 0, 1);
	} else {
		dnp->n_attrstamp = 0;
		mtx_unlock(&dnp->n_mtx);
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(dvp);
	}
	/*
	 * If negative lookup caching is enabled, I might as well
	 * add an entry for this node. Not necessary for correctness,
	 * but if negative caching is enabled, then the system
	 * must care about lookup caching hit rate, so...
	 */
	if (VFSTONFS(dvp->v_mount)->nm_negnametimeo != 0 &&
	    (cnp->cn_flags & MAKEENTRY) && attrflag != 0 && error == 0) {
		cache_enter_time(dvp, newvp, cnp, &nfsva.na_ctime, NULL);
	}
	return (error);
}

/*
 * nfs make dir call
 */
static int
nfs_mkdir(struct vop_mkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct componentname *cnp = ap->a_cnp;
	struct nfsnode *np = NULL, *dnp;
	struct vnode *newvp = NULL;
	struct vattr vattr;
	struct nfsfh *nfhp;
	struct nfsvattr nfsva, dnfsva;
	int error = 0, attrflag, dattrflag, ret;

	if ((error = VOP_GETATTR(dvp, &vattr, cnp->cn_cred)) != 0)
		return (error);
	vap->va_type = VDIR;
	error = nfsrpc_mkdir(dvp, cnp->cn_nameptr, cnp->cn_namelen,
	    vap, cnp->cn_cred, cnp->cn_thread, &dnfsva, &nfsva, &nfhp,
	    &attrflag, &dattrflag, NULL);
	dnp = VTONFS(dvp);
	mtx_lock(&dnp->n_mtx);
	dnp->n_flag |= NMODIFIED;
	if (dattrflag != 0) {
		mtx_unlock(&dnp->n_mtx);
		(void) nfscl_loadattrcache(&dvp, &dnfsva, NULL, NULL, 0, 1);
	} else {
		dnp->n_attrstamp = 0;
		mtx_unlock(&dnp->n_mtx);
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(dvp);
	}
	if (nfhp) {
		ret = nfscl_nget(dvp->v_mount, dvp, nfhp, cnp, cnp->cn_thread,
		    &np, NULL, LK_EXCLUSIVE);
		if (!ret) {
			newvp = NFSTOV(np);
			if (attrflag)
			   (void) nfscl_loadattrcache(&newvp, &nfsva, NULL,
				NULL, 0, 1);
		} else if (!error)
			error = ret;
	}
	if (!error && newvp == NULL) {
		error = nfs_lookitup(dvp, cnp->cn_nameptr, cnp->cn_namelen,
		    cnp->cn_cred, cnp->cn_thread, &np);
		if (!error) {
			newvp = NFSTOV(np);
			if (newvp->v_type != VDIR)
				error = EEXIST;
		}
	}
	if (error) {
		if (newvp)
			vput(newvp);
		if (NFS_ISV4(dvp))
			error = nfscl_maperr(cnp->cn_thread, error,
			    vap->va_uid, vap->va_gid);
	} else {
		/*
		 * If negative lookup caching is enabled, I might as well
		 * add an entry for this node. Not necessary for correctness,
		 * but if negative caching is enabled, then the system
		 * must care about lookup caching hit rate, so...
		 */
		if (VFSTONFS(dvp->v_mount)->nm_negnametimeo != 0 &&
		    (cnp->cn_flags & MAKEENTRY) &&
		    attrflag != 0 && dattrflag != 0)
			cache_enter_time(dvp, newvp, cnp, &nfsva.na_ctime,
			    &dnfsva.na_ctime);
		*ap->a_vpp = newvp;
	}
	return (error);
}

/*
 * nfs remove directory call
 */
static int
nfs_rmdir(struct vop_rmdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct nfsnode *dnp;
	struct nfsvattr dnfsva;
	int error, dattrflag;

	if (dvp == vp)
		return (EINVAL);
	error = nfsrpc_rmdir(dvp, cnp->cn_nameptr, cnp->cn_namelen,
	    cnp->cn_cred, cnp->cn_thread, &dnfsva, &dattrflag, NULL);
	dnp = VTONFS(dvp);
	mtx_lock(&dnp->n_mtx);
	dnp->n_flag |= NMODIFIED;
	if (dattrflag != 0) {
		mtx_unlock(&dnp->n_mtx);
		(void) nfscl_loadattrcache(&dvp, &dnfsva, NULL, NULL, 0, 1);
	} else {
		dnp->n_attrstamp = 0;
		mtx_unlock(&dnp->n_mtx);
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(dvp);
	}

	cache_purge(dvp);
	cache_purge(vp);
	if (error && NFS_ISV4(dvp))
		error = nfscl_maperr(cnp->cn_thread, error, (uid_t)0,
		    (gid_t)0);
	/*
	 * Kludge: Map ENOENT => 0 assuming that you have a reply to a retry.
	 */
	if (error == ENOENT)
		error = 0;
	return (error);
}

/*
 * nfs readdir call
 */
static int
nfs_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct uio *uio = ap->a_uio;
	ssize_t tresid, left;
	int error = 0;
	struct vattr vattr;
	
	if (ap->a_eofflag != NULL)
		*ap->a_eofflag = 0;
	if (vp->v_type != VDIR) 
		return(EPERM);

	/*
	 * First, check for hit on the EOF offset cache
	 */
	if (np->n_direofoffset > 0 && uio->uio_offset >= np->n_direofoffset &&
	    (np->n_flag & NMODIFIED) == 0) {
		if (VOP_GETATTR(vp, &vattr, ap->a_cred) == 0) {
			mtx_lock(&np->n_mtx);
			if ((NFS_ISV4(vp) && np->n_change == vattr.va_filerev) ||
			    !NFS_TIMESPEC_COMPARE(&np->n_mtime, &vattr.va_mtime)) {
				mtx_unlock(&np->n_mtx);
				NFSINCRGLOBAL(nfsstatsv1.direofcache_hits);
				if (ap->a_eofflag != NULL)
					*ap->a_eofflag = 1;
				return (0);
			} else
				mtx_unlock(&np->n_mtx);
		}
	}

	/*
	 * NFS always guarantees that directory entries don't straddle
	 * DIRBLKSIZ boundaries.  As such, we need to limit the size
	 * to an exact multiple of DIRBLKSIZ, to avoid copying a partial
	 * directory entry.
	 */
	left = uio->uio_resid % DIRBLKSIZ;
	if (left == uio->uio_resid)
		return (EINVAL);
	uio->uio_resid -= left;

	/*
	 * Call ncl_bioread() to do the real work.
	 */
	tresid = uio->uio_resid;
	error = ncl_bioread(vp, uio, 0, ap->a_cred);

	if (!error && uio->uio_resid == tresid) {
		NFSINCRGLOBAL(nfsstatsv1.direofcache_misses);
		if (ap->a_eofflag != NULL)
			*ap->a_eofflag = 1;
	}
	
	/* Add the partial DIRBLKSIZ (left) back in. */
	uio->uio_resid += left;
	return (error);
}

/*
 * Readdir rpc call.
 * Called from below the buffer cache by ncl_doio().
 */
int
ncl_readdirrpc(struct vnode *vp, struct uio *uiop, struct ucred *cred,
    struct thread *td)
{
	struct nfsvattr nfsva;
	nfsuint64 *cookiep, cookie;
	struct nfsnode *dnp = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error = 0, eof, attrflag;

	KASSERT(uiop->uio_iovcnt == 1 &&
	    (uiop->uio_offset & (DIRBLKSIZ - 1)) == 0 &&
	    (uiop->uio_resid & (DIRBLKSIZ - 1)) == 0,
	    ("nfs readdirrpc bad uio"));

	/*
	 * If there is no cookie, assume directory was stale.
	 */
	ncl_dircookie_lock(dnp);
	cookiep = ncl_getcookie(dnp, uiop->uio_offset, 0);
	if (cookiep) {
		cookie = *cookiep;
		ncl_dircookie_unlock(dnp);
	} else {
		ncl_dircookie_unlock(dnp);		
		return (NFSERR_BAD_COOKIE);
	}

	if (NFSHASNFSV3(nmp) && !NFSHASGOTFSINFO(nmp))
		(void)ncl_fsinfo(nmp, vp, cred, td);

	error = nfsrpc_readdir(vp, uiop, &cookie, cred, td, &nfsva,
	    &attrflag, &eof, NULL);
	if (attrflag)
		(void) nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 0, 1);

	if (!error) {
		/*
		 * We are now either at the end of the directory or have filled
		 * the block.
		 */
		if (eof)
			dnp->n_direofoffset = uiop->uio_offset;
		else {
			if (uiop->uio_resid > 0)
				printf("EEK! readdirrpc resid > 0\n");
			ncl_dircookie_lock(dnp);
			cookiep = ncl_getcookie(dnp, uiop->uio_offset, 1);
			*cookiep = cookie;
			ncl_dircookie_unlock(dnp);
		}
	} else if (NFS_ISV4(vp)) {
		error = nfscl_maperr(td, error, (uid_t)0, (gid_t)0);
	}
	return (error);
}

/*
 * NFS V3 readdir plus RPC. Used in place of ncl_readdirrpc().
 */
int
ncl_readdirplusrpc(struct vnode *vp, struct uio *uiop, struct ucred *cred,
    struct thread *td)
{
	struct nfsvattr nfsva;
	nfsuint64 *cookiep, cookie;
	struct nfsnode *dnp = VTONFS(vp);
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error = 0, attrflag, eof;

	KASSERT(uiop->uio_iovcnt == 1 &&
	    (uiop->uio_offset & (DIRBLKSIZ - 1)) == 0 &&
	    (uiop->uio_resid & (DIRBLKSIZ - 1)) == 0,
	    ("nfs readdirplusrpc bad uio"));

	/*
	 * If there is no cookie, assume directory was stale.
	 */
	ncl_dircookie_lock(dnp);
	cookiep = ncl_getcookie(dnp, uiop->uio_offset, 0);
	if (cookiep) {
		cookie = *cookiep;
		ncl_dircookie_unlock(dnp);
	} else {
		ncl_dircookie_unlock(dnp);
		return (NFSERR_BAD_COOKIE);
	}

	if (NFSHASNFSV3(nmp) && !NFSHASGOTFSINFO(nmp))
		(void)ncl_fsinfo(nmp, vp, cred, td);
	error = nfsrpc_readdirplus(vp, uiop, &cookie, cred, td, &nfsva,
	    &attrflag, &eof, NULL);
	if (attrflag)
		(void) nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 0, 1);

	if (!error) {
		/*
		 * We are now either at end of the directory or have filled the
		 * the block.
		 */
		if (eof)
			dnp->n_direofoffset = uiop->uio_offset;
		else {
			if (uiop->uio_resid > 0)
				printf("EEK! readdirplusrpc resid > 0\n");
			ncl_dircookie_lock(dnp);
			cookiep = ncl_getcookie(dnp, uiop->uio_offset, 1);
			*cookiep = cookie;
			ncl_dircookie_unlock(dnp);
		}
	} else if (NFS_ISV4(vp)) {
		error = nfscl_maperr(td, error, (uid_t)0, (gid_t)0);
	}
	return (error);
}

/*
 * Silly rename. To make the NFS filesystem that is stateless look a little
 * more like the "ufs" a remove of an active vnode is translated to a rename
 * to a funny looking filename that is removed by nfs_inactive on the
 * nfsnode. There is the potential for another process on a different client
 * to create the same funny name between the nfs_lookitup() fails and the
 * nfs_rename() completes, but...
 */
static int
nfs_sillyrename(struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
	struct sillyrename *sp;
	struct nfsnode *np;
	int error;
	short pid;
	unsigned int lticks;

	cache_purge(dvp);
	np = VTONFS(vp);
	KASSERT(vp->v_type != VDIR, ("nfs: sillyrename dir"));
	sp = malloc(sizeof (struct sillyrename),
	    M_NEWNFSREQ, M_WAITOK);
	sp->s_cred = crhold(cnp->cn_cred);
	sp->s_dvp = dvp;
	VREF(dvp);

	/* 
	 * Fudge together a funny name.
	 * Changing the format of the funny name to accommodate more 
	 * sillynames per directory.
	 * The name is now changed to .nfs.<ticks>.<pid>.4, where ticks is 
	 * CPU ticks since boot.
	 */
	pid = cnp->cn_thread->td_proc->p_pid;
	lticks = (unsigned int)ticks;
	for ( ; ; ) {
		sp->s_namlen = sprintf(sp->s_name, 
				       ".nfs.%08x.%04x4.4", lticks, 
				       pid);
		if (nfs_lookitup(dvp, sp->s_name, sp->s_namlen, sp->s_cred,
				 cnp->cn_thread, NULL))
			break;
		lticks++;
	}
	error = nfs_renameit(dvp, vp, cnp, sp);
	if (error)
		goto bad;
	error = nfs_lookitup(dvp, sp->s_name, sp->s_namlen, sp->s_cred,
		cnp->cn_thread, &np);
	np->n_sillyrename = sp;
	return (0);
bad:
	vrele(sp->s_dvp);
	crfree(sp->s_cred);
	free(sp, M_NEWNFSREQ);
	return (error);
}

/*
 * Look up a file name and optionally either update the file handle or
 * allocate an nfsnode, depending on the value of npp.
 * npp == NULL	--> just do the lookup
 * *npp == NULL --> allocate a new nfsnode and make sure attributes are
 *			handled too
 * *npp != NULL --> update the file handle in the vnode
 */
static int
nfs_lookitup(struct vnode *dvp, char *name, int len, struct ucred *cred,
    struct thread *td, struct nfsnode **npp)
{
	struct vnode *newvp = NULL, *vp;
	struct nfsnode *np, *dnp = VTONFS(dvp);
	struct nfsfh *nfhp, *onfhp;
	struct nfsvattr nfsva, dnfsva;
	struct componentname cn;
	int error = 0, attrflag, dattrflag;
	u_int hash;

	error = nfsrpc_lookup(dvp, name, len, cred, td, &dnfsva, &nfsva,
	    &nfhp, &attrflag, &dattrflag, NULL);
	if (dattrflag)
		(void) nfscl_loadattrcache(&dvp, &dnfsva, NULL, NULL, 0, 1);
	if (npp && !error) {
		if (*npp != NULL) {
		    np = *npp;
		    vp = NFSTOV(np);
		    /*
		     * For NFSv4, check to see if it is the same name and
		     * replace the name, if it is different.
		     */
		    if (np->n_v4 != NULL && nfsva.na_type == VREG &&
			(np->n_v4->n4_namelen != len ||
			 NFSBCMP(name, NFS4NODENAME(np->n_v4), len) ||
			 dnp->n_fhp->nfh_len != np->n_v4->n4_fhlen ||
			 NFSBCMP(dnp->n_fhp->nfh_fh, np->n_v4->n4_data,
			 dnp->n_fhp->nfh_len))) {
#ifdef notdef
{ char nnn[100]; int nnnl;
nnnl = (len < 100) ? len : 99;
bcopy(name, nnn, nnnl);
nnn[nnnl] = '\0';
printf("replace=%s\n",nnn);
}
#endif
			    free(np->n_v4, M_NFSV4NODE);
			    np->n_v4 = malloc(
				sizeof (struct nfsv4node) +
				dnp->n_fhp->nfh_len + len - 1,
				M_NFSV4NODE, M_WAITOK);
			    np->n_v4->n4_fhlen = dnp->n_fhp->nfh_len;
			    np->n_v4->n4_namelen = len;
			    NFSBCOPY(dnp->n_fhp->nfh_fh, np->n_v4->n4_data,
				dnp->n_fhp->nfh_len);
			    NFSBCOPY(name, NFS4NODENAME(np->n_v4), len);
		    }
		    hash = fnv_32_buf(nfhp->nfh_fh, nfhp->nfh_len,
			FNV1_32_INIT);
		    onfhp = np->n_fhp;
		    /*
		     * Rehash node for new file handle.
		     */
		    vfs_hash_rehash(vp, hash);
		    np->n_fhp = nfhp;
		    if (onfhp != NULL)
			free(onfhp, M_NFSFH);
		    newvp = NFSTOV(np);
		} else if (NFS_CMPFH(dnp, nfhp->nfh_fh, nfhp->nfh_len)) {
		    free(nfhp, M_NFSFH);
		    VREF(dvp);
		    newvp = dvp;
		} else {
		    cn.cn_nameptr = name;
		    cn.cn_namelen = len;
		    error = nfscl_nget(dvp->v_mount, dvp, nfhp, &cn, td,
			&np, NULL, LK_EXCLUSIVE);
		    if (error)
			return (error);
		    newvp = NFSTOV(np);
		}
		if (!attrflag && *npp == NULL) {
			if (newvp == dvp)
				vrele(newvp);
			else
				vput(newvp);
			return (ENOENT);
		}
		if (attrflag)
			(void) nfscl_loadattrcache(&newvp, &nfsva, NULL, NULL,
			    0, 1);
	}
	if (npp && *npp == NULL) {
		if (error) {
			if (newvp) {
				if (newvp == dvp)
					vrele(newvp);
				else
					vput(newvp);
			}
		} else
			*npp = np;
	}
	if (error && NFS_ISV4(dvp))
		error = nfscl_maperr(td, error, (uid_t)0, (gid_t)0);
	return (error);
}

/*
 * Nfs Version 3 and 4 commit rpc
 */
int
ncl_commit(struct vnode *vp, u_quad_t offset, int cnt, struct ucred *cred,
   struct thread *td)
{
	struct nfsvattr nfsva;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	struct nfsnode *np;
	struct uio uio;
	int error, attrflag;

	np = VTONFS(vp);
	error = EIO;
	attrflag = 0;
	if (NFSHASPNFS(nmp) && (np->n_flag & NDSCOMMIT) != 0) {
		uio.uio_offset = offset;
		uio.uio_resid = cnt;
		error = nfscl_doiods(vp, &uio, NULL, NULL,
		    NFSV4OPEN_ACCESSWRITE, 1, cred, td);
		if (error != 0) {
			mtx_lock(&np->n_mtx);
			np->n_flag &= ~NDSCOMMIT;
			mtx_unlock(&np->n_mtx);
		}
	}
	if (error != 0) {
		mtx_lock(&nmp->nm_mtx);
		if ((nmp->nm_state & NFSSTA_HASWRITEVERF) == 0) {
			mtx_unlock(&nmp->nm_mtx);
			return (0);
		}
		mtx_unlock(&nmp->nm_mtx);
		error = nfsrpc_commit(vp, offset, cnt, cred, td, &nfsva,
		    &attrflag, NULL);
	}
	if (attrflag != 0)
		(void) nfscl_loadattrcache(&vp, &nfsva, NULL, NULL,
		    0, 1);
	if (error != 0 && NFS_ISV4(vp))
		error = nfscl_maperr(td, error, (uid_t)0, (gid_t)0);
	return (error);
}

/*
 * Strategy routine.
 * For async requests when nfsiod(s) are running, queue the request by
 * calling ncl_asyncio(), otherwise just all ncl_doio() to do the
 * request.
 */
static int
nfs_strategy(struct vop_strategy_args *ap)
{
	struct buf *bp;
	struct vnode *vp;
	struct ucred *cr;

	bp = ap->a_bp;
	vp = ap->a_vp;
	KASSERT(bp->b_vp == vp, ("missing b_getvp"));
	KASSERT(!(bp->b_flags & B_DONE),
	    ("nfs_strategy: buffer %p unexpectedly marked B_DONE", bp));
	BUF_ASSERT_HELD(bp);

	if (vp->v_type == VREG && bp->b_blkno == bp->b_lblkno)
		bp->b_blkno = bp->b_lblkno * (vp->v_bufobj.bo_bsize /
		    DEV_BSIZE);
	if (bp->b_iocmd == BIO_READ)
		cr = bp->b_rcred;
	else
		cr = bp->b_wcred;

	/*
	 * If the op is asynchronous and an i/o daemon is waiting
	 * queue the request, wake it up and wait for completion
	 * otherwise just do it ourselves.
	 */
	if ((bp->b_flags & B_ASYNC) == 0 ||
	    ncl_asyncio(VFSTONFS(vp->v_mount), bp, NOCRED, curthread))
		(void) ncl_doio(vp, bp, cr, curthread, 1);
	return (0);
}

/*
 * fsync vnode op. Just call ncl_flush() with commit == 1.
 */
/* ARGSUSED */
static int
nfs_fsync(struct vop_fsync_args *ap)
{

	if (ap->a_vp->v_type != VREG) {
		/*
		 * For NFS, metadata is changed synchronously on the server,
		 * so there is nothing to flush. Also, ncl_flush() clears
		 * the NMODIFIED flag and that shouldn't be done here for
		 * directories.
		 */
		return (0);
	}
	return (ncl_flush(ap->a_vp, ap->a_waitfor, ap->a_td, 1, 0));
}

/*
 * Flush all the blocks associated with a vnode.
 * 	Walk through the buffer pool and push any dirty pages
 *	associated with the vnode.
 * If the called_from_renewthread argument is TRUE, it has been called
 * from the NFSv4 renew thread and, as such, cannot block indefinitely
 * waiting for a buffer write to complete.
 */
int
ncl_flush(struct vnode *vp, int waitfor, struct thread *td,
    int commit, int called_from_renewthread)
{
	struct nfsnode *np = VTONFS(vp);
	struct buf *bp;
	int i;
	struct buf *nbp;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int error = 0, slptimeo = 0, slpflag = 0, retv, bvecpos;
	int passone = 1, trycnt = 0;
	u_quad_t off, endoff, toff;
	struct ucred* wcred = NULL;
	struct buf **bvec = NULL;
	struct bufobj *bo;
#ifndef NFS_COMMITBVECSIZ
#define	NFS_COMMITBVECSIZ	20
#endif
	struct buf *bvec_on_stack[NFS_COMMITBVECSIZ];
	u_int bvecsize = 0, bveccount;

	if (called_from_renewthread != 0)
		slptimeo = hz;
	if (nmp->nm_flag & NFSMNT_INT)
		slpflag = PCATCH;
	if (!commit)
		passone = 0;
	bo = &vp->v_bufobj;
	/*
	 * A b_flags == (B_DELWRI | B_NEEDCOMMIT) block has been written to the
	 * server, but has not been committed to stable storage on the server
	 * yet. On the first pass, the byte range is worked out and the commit
	 * rpc is done. On the second pass, ncl_writebp() is called to do the
	 * job.
	 */
again:
	off = (u_quad_t)-1;
	endoff = 0;
	bvecpos = 0;
	if (NFS_ISV34(vp) && commit) {
		if (bvec != NULL && bvec != bvec_on_stack)
			free(bvec, M_TEMP);
		/*
		 * Count up how many buffers waiting for a commit.
		 */
		bveccount = 0;
		BO_LOCK(bo);
		TAILQ_FOREACH_SAFE(bp, &bo->bo_dirty.bv_hd, b_bobufs, nbp) {
			if (!BUF_ISLOCKED(bp) &&
			    (bp->b_flags & (B_DELWRI | B_NEEDCOMMIT))
				== (B_DELWRI | B_NEEDCOMMIT))
				bveccount++;
		}
		/*
		 * Allocate space to remember the list of bufs to commit.  It is
		 * important to use M_NOWAIT here to avoid a race with nfs_write.
		 * If we can't get memory (for whatever reason), we will end up
		 * committing the buffers one-by-one in the loop below.
		 */
		if (bveccount > NFS_COMMITBVECSIZ) {
			/*
			 * Release the vnode interlock to avoid a lock
			 * order reversal.
			 */
			BO_UNLOCK(bo);
			bvec = (struct buf **)
				malloc(bveccount * sizeof(struct buf *),
				       M_TEMP, M_NOWAIT);
			BO_LOCK(bo);
			if (bvec == NULL) {
				bvec = bvec_on_stack;
				bvecsize = NFS_COMMITBVECSIZ;
			} else
				bvecsize = bveccount;
		} else {
			bvec = bvec_on_stack;
			bvecsize = NFS_COMMITBVECSIZ;
		}
		TAILQ_FOREACH_SAFE(bp, &bo->bo_dirty.bv_hd, b_bobufs, nbp) {
			if (bvecpos >= bvecsize)
				break;
			if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT, NULL)) {
				nbp = TAILQ_NEXT(bp, b_bobufs);
				continue;
			}
			if ((bp->b_flags & (B_DELWRI | B_NEEDCOMMIT)) !=
			    (B_DELWRI | B_NEEDCOMMIT)) {
				BUF_UNLOCK(bp);
				nbp = TAILQ_NEXT(bp, b_bobufs);
				continue;
			}
			BO_UNLOCK(bo);
			bremfree(bp);
			/*
			 * Work out if all buffers are using the same cred
			 * so we can deal with them all with one commit.
			 *
			 * NOTE: we are not clearing B_DONE here, so we have
			 * to do it later on in this routine if we intend to
			 * initiate I/O on the bp.
			 *
			 * Note: to avoid loopback deadlocks, we do not
			 * assign b_runningbufspace.
			 */
			if (wcred == NULL)
				wcred = bp->b_wcred;
			else if (wcred != bp->b_wcred)
				wcred = NOCRED;
			vfs_busy_pages(bp, 1);

			BO_LOCK(bo);
			/*
			 * bp is protected by being locked, but nbp is not
			 * and vfs_busy_pages() may sleep.  We have to
			 * recalculate nbp.
			 */
			nbp = TAILQ_NEXT(bp, b_bobufs);

			/*
			 * A list of these buffers is kept so that the
			 * second loop knows which buffers have actually
			 * been committed. This is necessary, since there
			 * may be a race between the commit rpc and new
			 * uncommitted writes on the file.
			 */
			bvec[bvecpos++] = bp;
			toff = ((u_quad_t)bp->b_blkno) * DEV_BSIZE +
				bp->b_dirtyoff;
			if (toff < off)
				off = toff;
			toff += (u_quad_t)(bp->b_dirtyend - bp->b_dirtyoff);
			if (toff > endoff)
				endoff = toff;
		}
		BO_UNLOCK(bo);
	}
	if (bvecpos > 0) {
		/*
		 * Commit data on the server, as required.
		 * If all bufs are using the same wcred, then use that with
		 * one call for all of them, otherwise commit each one
		 * separately.
		 */
		if (wcred != NOCRED)
			retv = ncl_commit(vp, off, (int)(endoff - off),
					  wcred, td);
		else {
			retv = 0;
			for (i = 0; i < bvecpos; i++) {
				off_t off, size;
				bp = bvec[i];
				off = ((u_quad_t)bp->b_blkno) * DEV_BSIZE +
					bp->b_dirtyoff;
				size = (u_quad_t)(bp->b_dirtyend
						  - bp->b_dirtyoff);
				retv = ncl_commit(vp, off, (int)size,
						  bp->b_wcred, td);
				if (retv) break;
			}
		}

		if (retv == NFSERR_STALEWRITEVERF)
			ncl_clearcommit(vp->v_mount);

		/*
		 * Now, either mark the blocks I/O done or mark the
		 * blocks dirty, depending on whether the commit
		 * succeeded.
		 */
		for (i = 0; i < bvecpos; i++) {
			bp = bvec[i];
			bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
			if (retv) {
				/*
				 * Error, leave B_DELWRI intact
				 */
				vfs_unbusy_pages(bp);
				brelse(bp);
			} else {
				/*
				 * Success, remove B_DELWRI ( bundirty() ).
				 *
				 * b_dirtyoff/b_dirtyend seem to be NFS
				 * specific.  We should probably move that
				 * into bundirty(). XXX
				 */
				bufobj_wref(bo);
				bp->b_flags |= B_ASYNC;
				bundirty(bp);
				bp->b_flags &= ~B_DONE;
				bp->b_ioflags &= ~BIO_ERROR;
				bp->b_dirtyoff = bp->b_dirtyend = 0;
				bufdone(bp);
			}
		}
	}

	/*
	 * Start/do any write(s) that are required.
	 */
loop:
	BO_LOCK(bo);
	TAILQ_FOREACH_SAFE(bp, &bo->bo_dirty.bv_hd, b_bobufs, nbp) {
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT, NULL)) {
			if (waitfor != MNT_WAIT || passone)
				continue;

			error = BUF_TIMELOCK(bp,
			    LK_EXCLUSIVE | LK_SLEEPFAIL | LK_INTERLOCK,
			    BO_LOCKPTR(bo), "nfsfsync", slpflag, slptimeo);
			if (error == 0) {
				BUF_UNLOCK(bp);
				goto loop;
			}
			if (error == ENOLCK) {
				error = 0;
				goto loop;
			}
			if (called_from_renewthread != 0) {
				/*
				 * Return EIO so the flush will be retried
				 * later.
				 */
				error = EIO;
				goto done;
			}
			if (newnfs_sigintr(nmp, td)) {
				error = EINTR;
				goto done;
			}
			if (slpflag == PCATCH) {
				slpflag = 0;
				slptimeo = 2 * hz;
			}
			goto loop;
		}
		if ((bp->b_flags & B_DELWRI) == 0)
			panic("nfs_fsync: not dirty");
		if ((passone || !commit) && (bp->b_flags & B_NEEDCOMMIT)) {
			BUF_UNLOCK(bp);
			continue;
		}
		BO_UNLOCK(bo);
		bremfree(bp);
		if (passone || !commit)
		    bp->b_flags |= B_ASYNC;
		else
		    bp->b_flags |= B_ASYNC;
		bwrite(bp);
		if (newnfs_sigintr(nmp, td)) {
			error = EINTR;
			goto done;
		}
		goto loop;
	}
	if (passone) {
		passone = 0;
		BO_UNLOCK(bo);
		goto again;
	}
	if (waitfor == MNT_WAIT) {
		while (bo->bo_numoutput) {
			error = bufobj_wwait(bo, slpflag, slptimeo);
			if (error) {
			    BO_UNLOCK(bo);
			    if (called_from_renewthread != 0) {
				/*
				 * Return EIO so that the flush will be
				 * retried later.
				 */
				error = EIO;
				goto done;
			    }
			    error = newnfs_sigintr(nmp, td);
			    if (error)
				goto done;
			    if (slpflag == PCATCH) {
				slpflag = 0;
				slptimeo = 2 * hz;
			    }
			    BO_LOCK(bo);
			}
		}
		if (bo->bo_dirty.bv_cnt != 0 && commit) {
			BO_UNLOCK(bo);
			goto loop;
		}
		/*
		 * Wait for all the async IO requests to drain
		 */
		BO_UNLOCK(bo);
		mtx_lock(&np->n_mtx);
		while (np->n_directio_asyncwr > 0) {
			np->n_flag |= NFSYNCWAIT;
			error = newnfs_msleep(td, &np->n_directio_asyncwr,
			    &np->n_mtx, slpflag | (PRIBIO + 1), 
			    "nfsfsync", 0);
			if (error) {
				if (newnfs_sigintr(nmp, td)) {
					mtx_unlock(&np->n_mtx);
					error = EINTR;	
					goto done;
				}
			}
		}
		mtx_unlock(&np->n_mtx);
	} else
		BO_UNLOCK(bo);
	if (NFSHASPNFS(nmp)) {
		nfscl_layoutcommit(vp, td);
		/*
		 * Invalidate the attribute cache, since writes to a DS
		 * won't update the size attribute.
		 */
		mtx_lock(&np->n_mtx);
		np->n_attrstamp = 0;
	} else
		mtx_lock(&np->n_mtx);
	if (np->n_flag & NWRITEERR) {
		error = np->n_error;
		np->n_flag &= ~NWRITEERR;
	}
  	if (commit && bo->bo_dirty.bv_cnt == 0 &&
	    bo->bo_numoutput == 0 && np->n_directio_asyncwr == 0)
  		np->n_flag &= ~NMODIFIED;
	mtx_unlock(&np->n_mtx);
done:
	if (bvec != NULL && bvec != bvec_on_stack)
		free(bvec, M_TEMP);
	if (error == 0 && commit != 0 && waitfor == MNT_WAIT &&
	    (bo->bo_dirty.bv_cnt != 0 || bo->bo_numoutput != 0 ||
	    np->n_directio_asyncwr != 0)) {
		if (trycnt++ < 5) {
			/* try, try again... */
			passone = 1;
			wcred = NULL;
			bvec = NULL;
			bvecsize = 0;
			goto again;
		}
		vn_printf(vp, "ncl_flush failed");
		error = called_from_renewthread != 0 ? EIO : EBUSY;
	}
	return (error);
}

/*
 * NFS advisory byte-level locks.
 */
static int
nfs_advlock(struct vop_advlock_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct ucred *cred;
	struct nfsnode *np = VTONFS(ap->a_vp);
	struct proc *p = (struct proc *)ap->a_id;
	struct thread *td = curthread;	/* XXX */
	struct vattr va;
	int ret, error;
	u_quad_t size;
	
	error = NFSVOPLOCK(vp, LK_SHARED);
	if (error != 0)
		return (EBADF);
	if (NFS_ISV4(vp) && (ap->a_flags & (F_POSIX | F_FLOCK)) != 0) {
		if (vp->v_type != VREG) {
			error = EINVAL;
			goto out;
		}
		if ((ap->a_flags & F_POSIX) != 0)
			cred = p->p_ucred;
		else
			cred = td->td_ucred;
		NFSVOPLOCK(vp, LK_UPGRADE | LK_RETRY);
		if (vp->v_iflag & VI_DOOMED) {
			error = EBADF;
			goto out;
		}

		/*
		 * If this is unlocking a write locked region, flush and
		 * commit them before unlocking. This is required by
		 * RFC3530 Sec. 9.3.2.
		 */
		if (ap->a_op == F_UNLCK &&
		    nfscl_checkwritelocked(vp, ap->a_fl, cred, td, ap->a_id,
		    ap->a_flags))
			(void) ncl_flush(vp, MNT_WAIT, td, 1, 0);

		/*
		 * Loop around doing the lock op, while a blocking lock
		 * must wait for the lock op to succeed.
		 */
		do {
			ret = nfsrpc_advlock(vp, np->n_size, ap->a_op,
			    ap->a_fl, 0, cred, td, ap->a_id, ap->a_flags);
			if (ret == NFSERR_DENIED && (ap->a_flags & F_WAIT) &&
			    ap->a_op == F_SETLK) {
				NFSVOPUNLOCK(vp, 0);
				error = nfs_catnap(PZERO | PCATCH, ret,
				    "ncladvl");
				if (error)
					return (EINTR);
				NFSVOPLOCK(vp, LK_EXCLUSIVE | LK_RETRY);
				if (vp->v_iflag & VI_DOOMED) {
					error = EBADF;
					goto out;
				}
			}
		} while (ret == NFSERR_DENIED && (ap->a_flags & F_WAIT) &&
		     ap->a_op == F_SETLK);
		if (ret == NFSERR_DENIED) {
			error = EAGAIN;
			goto out;
		} else if (ret == EINVAL || ret == EBADF || ret == EINTR) {
			error = ret;
			goto out;
		} else if (ret != 0) {
			error = EACCES;
			goto out;
		}

		/*
		 * Now, if we just got a lock, invalidate data in the buffer
		 * cache, as required, so that the coherency conforms with
		 * RFC3530 Sec. 9.3.2.
		 */
		if (ap->a_op == F_SETLK) {
			if ((np->n_flag & NMODIFIED) == 0) {
				np->n_attrstamp = 0;
				KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp);
				ret = VOP_GETATTR(vp, &va, cred);
			}
			if ((np->n_flag & NMODIFIED) || ret ||
			    np->n_change != va.va_filerev) {
				(void) ncl_vinvalbuf(vp, V_SAVE, td, 1);
				np->n_attrstamp = 0;
				KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp);
				ret = VOP_GETATTR(vp, &va, cred);
				if (!ret) {
					np->n_mtime = va.va_mtime;
					np->n_change = va.va_filerev;
				}
			}
			/* Mark that a file lock has been acquired. */
			mtx_lock(&np->n_mtx);
			np->n_flag |= NHASBEENLOCKED;
			mtx_unlock(&np->n_mtx);
		}
	} else if (!NFS_ISV4(vp)) {
		if ((VFSTONFS(vp->v_mount)->nm_flag & NFSMNT_NOLOCKD) != 0) {
			size = VTONFS(vp)->n_size;
			NFSVOPUNLOCK(vp, 0);
			error = lf_advlock(ap, &(vp->v_lockf), size);
		} else {
			if (nfs_advlock_p != NULL)
				error = nfs_advlock_p(ap);
			else {
				NFSVOPUNLOCK(vp, 0);
				error = ENOLCK;
			}
		}
		if (error == 0 && ap->a_op == F_SETLK) {
			error = NFSVOPLOCK(vp, LK_SHARED);
			if (error == 0) {
				/* Mark that a file lock has been acquired. */
				mtx_lock(&np->n_mtx);
				np->n_flag |= NHASBEENLOCKED;
				mtx_unlock(&np->n_mtx);
				NFSVOPUNLOCK(vp, 0);
			}
		}
		return (error);
	} else
		error = EOPNOTSUPP;
out:
	NFSVOPUNLOCK(vp, 0);
	return (error);
}

/*
 * NFS advisory byte-level locks.
 */
static int
nfs_advlockasync(struct vop_advlockasync_args *ap)
{
	struct vnode *vp = ap->a_vp;
	u_quad_t size;
	int error;
	
	if (NFS_ISV4(vp))
		return (EOPNOTSUPP);
	error = NFSVOPLOCK(vp, LK_SHARED);
	if (error)
		return (error);
	if ((VFSTONFS(vp->v_mount)->nm_flag & NFSMNT_NOLOCKD) != 0) {
		size = VTONFS(vp)->n_size;
		NFSVOPUNLOCK(vp, 0);
		error = lf_advlockasync(ap, &(vp->v_lockf), size);
	} else {
		NFSVOPUNLOCK(vp, 0);
		error = EOPNOTSUPP;
	}
	return (error);
}

/*
 * Print out the contents of an nfsnode.
 */
static int
nfs_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);

	printf("\tfileid %jd fsid 0x%jx", (uintmax_t)np->n_vattr.na_fileid,
	    (uintmax_t)np->n_vattr.na_fsid);
	if (vp->v_type == VFIFO)
		fifo_printinfo(vp);
	printf("\n");
	return (0);
}

/*
 * This is the "real" nfs::bwrite(struct buf*).
 * We set B_CACHE if this is a VMIO buffer.
 */
int
ncl_writebp(struct buf *bp, int force __unused, struct thread *td)
{
	int oldflags, rtval;

	BUF_ASSERT_HELD(bp);

	if (bp->b_flags & B_INVAL) {
		brelse(bp);
		return (0);
	}

	oldflags = bp->b_flags;
	bp->b_flags |= B_CACHE;

	/*
	 * Undirty the bp.  We will redirty it later if the I/O fails.
	 */
	bundirty(bp);
	bp->b_flags &= ~B_DONE;
	bp->b_ioflags &= ~BIO_ERROR;
	bp->b_iocmd = BIO_WRITE;

	bufobj_wref(bp->b_bufobj);
	curthread->td_ru.ru_oublock++;

	/*
	 * Note: to avoid loopback deadlocks, we do not
	 * assign b_runningbufspace.
	 */
	vfs_busy_pages(bp, 1);

	BUF_KERNPROC(bp);
	bp->b_iooffset = dbtob(bp->b_blkno);
	bstrategy(bp);

	if ((oldflags & B_ASYNC) != 0)
		return (0);

	rtval = bufwait(bp);
	if (oldflags & B_DELWRI)
		reassignbuf(bp);
	brelse(bp);
	return (rtval);
}

/*
 * nfs special file access vnode op.
 * Essentially just get vattr and then imitate iaccess() since the device is
 * local to the client.
 */
static int
nfsspec_access(struct vop_access_args *ap)
{
	struct vattr *vap;
	struct ucred *cred = ap->a_cred;
	struct vnode *vp = ap->a_vp;
	accmode_t accmode = ap->a_accmode;
	struct vattr vattr;
	int error;

	/*
	 * Disallow write attempts on filesystems mounted read-only;
	 * unless the file is a socket, fifo, or a block or character
	 * device resident on the filesystem.
	 */
	if ((accmode & VWRITE) && (vp->v_mount->mnt_flag & MNT_RDONLY)) {
		switch (vp->v_type) {
		case VREG:
		case VDIR:
		case VLNK:
			return (EROFS);
		default:
			break;
		}
	}
	vap = &vattr;
	error = VOP_GETATTR(vp, vap, cred);
	if (error)
		goto out;
	error  = vaccess(vp->v_type, vap->va_mode, vap->va_uid, vap->va_gid,
	    accmode, cred, NULL);
out:
	return error;
}

/*
 * Read wrapper for fifos.
 */
static int
nfsfifo_read(struct vop_read_args *ap)
{
	struct nfsnode *np = VTONFS(ap->a_vp);
	int error;

	/*
	 * Set access flag.
	 */
	mtx_lock(&np->n_mtx);
	np->n_flag |= NACC;
	vfs_timestamp(&np->n_atim);
	mtx_unlock(&np->n_mtx);
	error = fifo_specops.vop_read(ap);
	return error;	
}

/*
 * Write wrapper for fifos.
 */
static int
nfsfifo_write(struct vop_write_args *ap)
{
	struct nfsnode *np = VTONFS(ap->a_vp);

	/*
	 * Set update flag.
	 */
	mtx_lock(&np->n_mtx);
	np->n_flag |= NUPD;
	vfs_timestamp(&np->n_mtim);
	mtx_unlock(&np->n_mtx);
	return(fifo_specops.vop_write(ap));
}

/*
 * Close wrapper for fifos.
 *
 * Update the times on the nfsnode then do fifo close.
 */
static int
nfsfifo_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np = VTONFS(vp);
	struct vattr vattr;
	struct timespec ts;

	mtx_lock(&np->n_mtx);
	if (np->n_flag & (NACC | NUPD)) {
		vfs_timestamp(&ts);
		if (np->n_flag & NACC)
			np->n_atim = ts;
		if (np->n_flag & NUPD)
			np->n_mtim = ts;
		np->n_flag |= NCHG;
		if (vrefcnt(vp) == 1 &&
		    (vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
			VATTR_NULL(&vattr);
			if (np->n_flag & NACC)
				vattr.va_atime = np->n_atim;
			if (np->n_flag & NUPD)
				vattr.va_mtime = np->n_mtim;
			mtx_unlock(&np->n_mtx);
			(void)VOP_SETATTR(vp, &vattr, ap->a_cred);
			goto out;
		}
	}
	mtx_unlock(&np->n_mtx);
out:
	return (fifo_specops.vop_close(ap));
}

/*
 * Just call ncl_writebp() with the force argument set to 1.
 *
 * NOTE: B_DONE may or may not be set in a_bp on call.
 */
static int
nfs_bwrite(struct buf *bp)
{

	return (ncl_writebp(bp, 1, curthread));
}

struct buf_ops buf_ops_newnfs = {
	.bop_name	=	"buf_ops_nfs",
	.bop_write	=	nfs_bwrite,
	.bop_strategy	=	bufstrategy,
	.bop_sync	=	bufsync,
	.bop_bdflush	=	bufbdflush,
};

static int
nfs_getacl(struct vop_getacl_args *ap)
{
	int error;

	if (ap->a_type != ACL_TYPE_NFS4)
		return (EOPNOTSUPP);
	error = nfsrpc_getacl(ap->a_vp, ap->a_cred, ap->a_td, ap->a_aclp,
	    NULL);
	if (error > NFSERR_STALE) {
		(void) nfscl_maperr(ap->a_td, error, (uid_t)0, (gid_t)0);
		error = EPERM;
	}
	return (error);
}

static int
nfs_setacl(struct vop_setacl_args *ap)
{
	int error;

	if (ap->a_type != ACL_TYPE_NFS4)
		return (EOPNOTSUPP);
	error = nfsrpc_setacl(ap->a_vp, ap->a_cred, ap->a_td, ap->a_aclp,
	    NULL);
	if (error > NFSERR_STALE) {
		(void) nfscl_maperr(ap->a_td, error, (uid_t)0, (gid_t)0);
		error = EPERM;
	}
	return (error);
}

static int
nfs_set_text(struct vop_set_text_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nfsnode *np;

	/*
	 * If the text file has been mmap'd, flush any dirty pages to the
	 * buffer cache and then...
	 * Make sure all writes are pushed to the NFS server.  If this is not
	 * done, the modify time of the file can change while the text
	 * file is being executed.  This will cause the process that is
	 * executing the text file to be terminated.
	 */
	if (vp->v_object != NULL) {
		VM_OBJECT_WLOCK(vp->v_object);
		vm_object_page_clean(vp->v_object, 0, 0, OBJPC_SYNC);
		VM_OBJECT_WUNLOCK(vp->v_object);
	}

	/* Now, flush the buffer cache. */
	ncl_flush(vp, MNT_WAIT, curthread, 0, 0);

	/* And, finally, make sure that n_mtime is up to date. */
	np = VTONFS(vp);
	mtx_lock(&np->n_mtx);
	np->n_mtime = np->n_vattr.na_mtime;
	mtx_unlock(&np->n_mtx);

	vp->v_vflag |= VV_TEXT;
	return (0);
}

/*
 * Return POSIX pathconf information applicable to nfs filesystems.
 */
static int
nfs_pathconf(struct vop_pathconf_args *ap)
{
	struct nfsv3_pathconf pc;
	struct nfsvattr nfsva;
	struct vnode *vp = ap->a_vp;
	struct thread *td = curthread;
	int attrflag, error;

	if ((NFS_ISV34(vp) && (ap->a_name == _PC_LINK_MAX ||
	    ap->a_name == _PC_NAME_MAX || ap->a_name == _PC_CHOWN_RESTRICTED ||
	    ap->a_name == _PC_NO_TRUNC)) ||
	    (NFS_ISV4(vp) && ap->a_name == _PC_ACL_NFS4)) {
		/*
		 * Since only the above 4 a_names are returned by the NFSv3
		 * Pathconf RPC, there is no point in doing it for others.
		 * For NFSv4, the Pathconf RPC (actually a Getattr Op.) can
		 * be used for _PC_NFS4_ACL as well.
		 */
		error = nfsrpc_pathconf(vp, &pc, td->td_ucred, td, &nfsva,
		    &attrflag, NULL);
		if (attrflag != 0)
			(void) nfscl_loadattrcache(&vp, &nfsva, NULL, NULL, 0,
			    1);
		if (error != 0)
			return (error);
	} else {
		/*
		 * For NFSv2 (or NFSv3 when not one of the above 4 a_names),
		 * just fake them.
		 */
		pc.pc_linkmax = NFS_LINK_MAX;
		pc.pc_namemax = NFS_MAXNAMLEN;
		pc.pc_notrunc = 1;
		pc.pc_chownrestricted = 1;
		pc.pc_caseinsensitive = 0;
		pc.pc_casepreserving = 1;
		error = 0;
	}
	switch (ap->a_name) {
	case _PC_LINK_MAX:
#ifdef _LP64
		*ap->a_retval = pc.pc_linkmax;
#else
		*ap->a_retval = MIN(LONG_MAX, pc.pc_linkmax);
#endif
		break;
	case _PC_NAME_MAX:
		*ap->a_retval = pc.pc_namemax;
		break;
	case _PC_PIPE_BUF:
		if (ap->a_vp->v_type == VDIR || ap->a_vp->v_type == VFIFO)
			*ap->a_retval = PIPE_BUF;
		else
			error = EINVAL;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = pc.pc_chownrestricted;
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = pc.pc_notrunc;
		break;
	case _PC_ACL_NFS4:
		if (NFS_ISV4(vp) && nfsrv_useacl != 0 && attrflag != 0 &&
		    NFSISSET_ATTRBIT(&nfsva.na_suppattr, NFSATTRBIT_ACL))
			*ap->a_retval = 1;
		else
			*ap->a_retval = 0;
		break;
	case _PC_ACL_PATH_MAX:
		if (NFS_ISV4(vp))
			*ap->a_retval = ACL_MAX_ENTRIES;
		else
			*ap->a_retval = 3;
		break;
	case _PC_PRIO_IO:
		*ap->a_retval = 0;
		break;
	case _PC_SYNC_IO:
		*ap->a_retval = 0;
		break;
	case _PC_ALLOC_SIZE_MIN:
		*ap->a_retval = vp->v_mount->mnt_stat.f_bsize;
		break;
	case _PC_FILESIZEBITS:
		if (NFS_ISV34(vp))
			*ap->a_retval = 64;
		else
			*ap->a_retval = 32;
		break;
	case _PC_REC_INCR_XFER_SIZE:
		*ap->a_retval = vp->v_mount->mnt_stat.f_iosize;
		break;
	case _PC_REC_MAX_XFER_SIZE:
		*ap->a_retval = -1; /* means ``unlimited'' */
		break;
	case _PC_REC_MIN_XFER_SIZE:
		*ap->a_retval = vp->v_mount->mnt_stat.f_iosize;
		break;
	case _PC_REC_XFER_ALIGN:
		*ap->a_retval = PAGE_SIZE;
		break;
	case _PC_SYMLINK_MAX:
		*ap->a_retval = NFS_MAXPATHLEN;
		break;

	default:
		error = vop_stdpathconf(ap);
		break;
	}
	return (error);
}

