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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/capsicum.h>
#include <sys/extattr.h>

/*
 * Functions that perform the vfs operations required by the routines in
 * nfsd_serv.c. It is hoped that this change will make the server more
 * portable.
 */

#include <fs/nfs/nfsport.h>
#include <sys/hash.h>
#include <sys/sysctl.h>
#include <nlm/nlm_prot.h>
#include <nlm/nlm.h>

FEATURE(nfsd, "NFSv4 server");

extern u_int32_t newnfs_true, newnfs_false, newnfs_xdrneg1;
extern int nfsrv_useacl;
extern int newnfs_numnfsd;
extern struct mount nfsv4root_mnt;
extern struct nfsrv_stablefirst nfsrv_stablefirst;
extern void (*nfsd_call_servertimer)(void);
extern SVCPOOL	*nfsrvd_pool;
extern struct nfsv4lock nfsd_suspend_lock;
extern struct nfsclienthashhead *nfsclienthash;
extern struct nfslockhashhead *nfslockhash;
extern struct nfssessionhash *nfssessionhash;
extern int nfsrv_sessionhashsize;
extern struct nfsstatsv1 nfsstatsv1;
extern struct nfslayouthash *nfslayouthash;
extern int nfsrv_layouthashsize;
extern struct mtx nfsrv_dslock_mtx;
extern int nfs_pnfsiothreads;
extern struct nfsdontlisthead nfsrv_dontlisthead;
extern volatile int nfsrv_dontlistlen;
extern volatile int nfsrv_devidcnt;
extern int nfsrv_maxpnfsmirror;
struct vfsoptlist nfsv4root_opt, nfsv4root_newopt;
NFSDLOCKMUTEX;
NFSSTATESPINLOCK;
struct nfsrchash_bucket nfsrchash_table[NFSRVCACHE_HASHSIZE];
struct nfsrchash_bucket nfsrcahash_table[NFSRVCACHE_HASHSIZE];
struct mtx nfsrc_udpmtx;
struct mtx nfs_v4root_mutex;
struct mtx nfsrv_dontlistlock_mtx;
struct mtx nfsrv_recalllock_mtx;
struct nfsrvfh nfs_rootfh, nfs_pubfh;
int nfs_pubfhset = 0, nfs_rootfhset = 0;
struct proc *nfsd_master_proc = NULL;
int nfsd_debuglevel = 0;
static pid_t nfsd_master_pid = (pid_t)-1;
static char nfsd_master_comm[MAXCOMLEN + 1];
static struct timeval nfsd_master_start;
static uint32_t nfsv4_sysid = 0;
static fhandle_t zerofh;

static int nfssvc_srvcall(struct thread *, struct nfssvc_args *,
    struct ucred *);

int nfsrv_enable_crossmntpt = 1;
static int nfs_commit_blks;
static int nfs_commit_miss;
extern int nfsrv_issuedelegs;
extern int nfsrv_dolocallocks;
extern int nfsd_enable_stringtouid;
extern struct nfsdevicehead nfsrv_devidhead;

static void nfsrv_pnfscreate(struct vnode *, struct vattr *, struct ucred *,
    NFSPROC_T *);
static void nfsrv_pnfsremovesetup(struct vnode *, NFSPROC_T *, struct vnode **,
    int *, char *, fhandle_t *);
static void nfsrv_pnfsremove(struct vnode **, int, char *, fhandle_t *,
    NFSPROC_T *);
static int nfsrv_proxyds(struct nfsrv_descript *, struct vnode *, off_t, int,
    struct ucred *, struct thread *, int, struct mbuf **, char *,
    struct mbuf **, struct nfsvattr *, struct acl *);
static int nfsrv_setextattr(struct vnode *, struct nfsvattr *, NFSPROC_T *);
static int nfsrv_readdsrpc(fhandle_t *, off_t, int, struct ucred *,
    NFSPROC_T *, struct nfsmount *, struct mbuf **, struct mbuf **);
static int nfsrv_writedsrpc(fhandle_t *, off_t, int, struct ucred *,
    NFSPROC_T *, struct vnode *, struct nfsmount **, int, struct mbuf **,
    char *, int *);
static int nfsrv_setacldsrpc(fhandle_t *, struct ucred *, NFSPROC_T *,
    struct vnode *, struct nfsmount **, int, struct acl *, int *);
static int nfsrv_setattrdsrpc(fhandle_t *, struct ucred *, NFSPROC_T *,
    struct vnode *, struct nfsmount **, int, struct nfsvattr *, int *);
static int nfsrv_getattrdsrpc(fhandle_t *, struct ucred *, NFSPROC_T *,
    struct vnode *, struct nfsmount *, struct nfsvattr *);
static int nfsrv_putfhname(fhandle_t *, char *);
static int nfsrv_pnfslookupds(struct vnode *, struct vnode *,
    struct pnfsdsfile *, struct vnode **, NFSPROC_T *);
static void nfsrv_pnfssetfh(struct vnode *, struct pnfsdsfile *, char *, char *,
    struct vnode *, NFSPROC_T *);
static int nfsrv_dsremove(struct vnode *, char *, struct ucred *, NFSPROC_T *);
static int nfsrv_dssetacl(struct vnode *, struct acl *, struct ucred *,
    NFSPROC_T *);
static int nfsrv_pnfsstatfs(struct statfs *, struct mount *);

int nfs_pnfsio(task_fn_t *, void *);

SYSCTL_NODE(_vfs, OID_AUTO, nfsd, CTLFLAG_RW, 0, "NFS server");
SYSCTL_INT(_vfs_nfsd, OID_AUTO, mirrormnt, CTLFLAG_RW,
    &nfsrv_enable_crossmntpt, 0, "Enable nfsd to cross mount points");
SYSCTL_INT(_vfs_nfsd, OID_AUTO, commit_blks, CTLFLAG_RW, &nfs_commit_blks,
    0, "");
SYSCTL_INT(_vfs_nfsd, OID_AUTO, commit_miss, CTLFLAG_RW, &nfs_commit_miss,
    0, "");
SYSCTL_INT(_vfs_nfsd, OID_AUTO, issue_delegations, CTLFLAG_RW,
    &nfsrv_issuedelegs, 0, "Enable nfsd to issue delegations");
SYSCTL_INT(_vfs_nfsd, OID_AUTO, enable_locallocks, CTLFLAG_RW,
    &nfsrv_dolocallocks, 0, "Enable nfsd to acquire local locks on files");
SYSCTL_INT(_vfs_nfsd, OID_AUTO, debuglevel, CTLFLAG_RW, &nfsd_debuglevel,
    0, "Debug level for NFS server");
SYSCTL_INT(_vfs_nfsd, OID_AUTO, enable_stringtouid, CTLFLAG_RW,
    &nfsd_enable_stringtouid, 0, "Enable nfsd to accept numeric owner_names");
static int nfsrv_pnfsgetdsattr = 1;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, pnfsgetdsattr, CTLFLAG_RW,
    &nfsrv_pnfsgetdsattr, 0, "When set getattr gets DS attributes via RPC");

/*
 * nfsrv_dsdirsize can only be increased and only when the nfsd threads are
 * not running.
 * The dsN subdirectories for the increased values must have been created
 * on all DS servers before this increase is done.
 */
u_int	nfsrv_dsdirsize = 20;
static int
sysctl_dsdirsize(SYSCTL_HANDLER_ARGS)
{
	int error, newdsdirsize;

	newdsdirsize = nfsrv_dsdirsize;
	error = sysctl_handle_int(oidp, &newdsdirsize, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (newdsdirsize <= nfsrv_dsdirsize || newdsdirsize > 10000 ||
	    newnfs_numnfsd != 0)
		return (EINVAL);
	nfsrv_dsdirsize = newdsdirsize;
	return (0);
}
SYSCTL_PROC(_vfs_nfsd, OID_AUTO, dsdirsize, CTLTYPE_UINT | CTLFLAG_RW, 0,
    sizeof(nfsrv_dsdirsize), sysctl_dsdirsize, "IU",
    "Number of dsN subdirs on the DS servers");

#define	MAX_REORDERED_RPC	16
#define	NUM_HEURISTIC		1031
#define	NHUSE_INIT		64
#define	NHUSE_INC		16
#define	NHUSE_MAX		2048

static struct nfsheur {
	struct vnode *nh_vp;	/* vp to match (unreferenced pointer) */
	off_t nh_nextoff;	/* next offset for sequential detection */
	int nh_use;		/* use count for selection */
	int nh_seqcount;	/* heuristic */
} nfsheur[NUM_HEURISTIC];


/*
 * Heuristic to detect sequential operation.
 */
static struct nfsheur *
nfsrv_sequential_heuristic(struct uio *uio, struct vnode *vp)
{
	struct nfsheur *nh;
	int hi, try;

	/* Locate best candidate. */
	try = 32;
	hi = ((int)(vm_offset_t)vp / sizeof(struct vnode)) % NUM_HEURISTIC;
	nh = &nfsheur[hi];
	while (try--) {
		if (nfsheur[hi].nh_vp == vp) {
			nh = &nfsheur[hi];
			break;
		}
		if (nfsheur[hi].nh_use > 0)
			--nfsheur[hi].nh_use;
		hi = (hi + 1) % NUM_HEURISTIC;
		if (nfsheur[hi].nh_use < nh->nh_use)
			nh = &nfsheur[hi];
	}

	/* Initialize hint if this is a new file. */
	if (nh->nh_vp != vp) {
		nh->nh_vp = vp;
		nh->nh_nextoff = uio->uio_offset;
		nh->nh_use = NHUSE_INIT;
		if (uio->uio_offset == 0)
			nh->nh_seqcount = 4;
		else
			nh->nh_seqcount = 1;
	}

	/* Calculate heuristic. */
	if ((uio->uio_offset == 0 && nh->nh_seqcount > 0) ||
	    uio->uio_offset == nh->nh_nextoff) {
		/* See comments in vfs_vnops.c:sequential_heuristic(). */
		nh->nh_seqcount += howmany(uio->uio_resid, 16384);
		if (nh->nh_seqcount > IO_SEQMAX)
			nh->nh_seqcount = IO_SEQMAX;
	} else if (qabs(uio->uio_offset - nh->nh_nextoff) <= MAX_REORDERED_RPC *
	    imax(vp->v_mount->mnt_stat.f_iosize, uio->uio_resid)) {
		/* Probably a reordered RPC, leave seqcount alone. */
	} else if (nh->nh_seqcount > 1) {
		nh->nh_seqcount /= 2;
	} else {
		nh->nh_seqcount = 0;
	}
	nh->nh_use += NHUSE_INC;
	if (nh->nh_use > NHUSE_MAX)
		nh->nh_use = NHUSE_MAX;
	return (nh);
}

/*
 * Get attributes into nfsvattr structure.
 */
int
nfsvno_getattr(struct vnode *vp, struct nfsvattr *nvap,
    struct nfsrv_descript *nd, struct thread *p, int vpislocked,
    nfsattrbit_t *attrbitp)
{
	int error, gotattr, lockedit = 0;
	struct nfsvattr na;

	if (vpislocked == 0) {
		/*
		 * When vpislocked == 0, the vnode is either exclusively
		 * locked by this thread or not locked by this thread.
		 * As such, shared lock it, if not exclusively locked.
		 */
		if (NFSVOPISLOCKED(vp) != LK_EXCLUSIVE) {
			lockedit = 1;
			NFSVOPLOCK(vp, LK_SHARED | LK_RETRY);
		}
	}

	/*
	 * Acquire the Change, Size and TimeModify attributes, as required.
	 * This needs to be done for regular files if:
	 * - non-NFSv4 RPCs or
	 * - when attrbitp == NULL or
	 * - an NFSv4 RPC with any of the above attributes in attrbitp.
	 * A return of 0 for nfsrv_proxyds() indicates that it has acquired
	 * these attributes.  nfsrv_proxyds() will return an error if the
	 * server is not a pNFS one.
	 */
	gotattr = 0;
	if (vp->v_type == VREG && nfsrv_devidcnt > 0 && (attrbitp == NULL ||
	    (nd->nd_flag & ND_NFSV4) == 0 ||
	    NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_CHANGE) ||
	    NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_SIZE) ||
	    NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_TIMEACCESS) ||
	    NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_TIMEMODIFY))) {
		error = nfsrv_proxyds(nd, vp, 0, 0, nd->nd_cred, p,
		    NFSPROC_GETATTR, NULL, NULL, NULL, &na, NULL);
		if (error == 0)
			gotattr = 1;
	}

	error = VOP_GETATTR(vp, &nvap->na_vattr, nd->nd_cred);
	if (lockedit != 0)
		NFSVOPUNLOCK(vp, 0);

	/*
	 * If we got the Change, Size and Modify Time from the DS,
	 * replace them.
	 */
	if (gotattr != 0) {
		nvap->na_atime = na.na_atime;
		nvap->na_mtime = na.na_mtime;
		nvap->na_filerev = na.na_filerev;
		nvap->na_size = na.na_size;
	}
	NFSD_DEBUG(4, "nfsvno_getattr: gotattr=%d err=%d chg=%ju\n", gotattr,
	    error, (uintmax_t)na.na_filerev);

	NFSEXITCODE(error);
	return (error);
}

/*
 * Get a file handle for a vnode.
 */
int
nfsvno_getfh(struct vnode *vp, fhandle_t *fhp, struct thread *p)
{
	int error;

	NFSBZERO((caddr_t)fhp, sizeof(fhandle_t));
	fhp->fh_fsid = vp->v_mount->mnt_stat.f_fsid;
	error = VOP_VPTOFH(vp, &fhp->fh_fid);

	NFSEXITCODE(error);
	return (error);
}

/*
 * Perform access checking for vnodes obtained from file handles that would
 * refer to files already opened by a Unix client. You cannot just use
 * vn_writechk() and VOP_ACCESSX() for two reasons.
 * 1 - You must check for exported rdonly as well as MNT_RDONLY for the write
 *     case.
 * 2 - The owner is to be given access irrespective of mode bits for some
 *     operations, so that processes that chmod after opening a file don't
 *     break.
 */
int
nfsvno_accchk(struct vnode *vp, accmode_t accmode, struct ucred *cred,
    struct nfsexstuff *exp, struct thread *p, int override, int vpislocked,
    u_int32_t *supportedtypep)
{
	struct vattr vattr;
	int error = 0, getret = 0;

	if (vpislocked == 0) {
		if (NFSVOPLOCK(vp, LK_SHARED) != 0) {
			error = EPERM;
			goto out;
		}
	}
	if (accmode & VWRITE) {
		/* Just vn_writechk() changed to check rdonly */
		/*
		 * Disallow write attempts on read-only file systems;
		 * unless the file is a socket or a block or character
		 * device resident on the file system.
		 */
		if (NFSVNO_EXRDONLY(exp) ||
		    (vp->v_mount->mnt_flag & MNT_RDONLY)) {
			switch (vp->v_type) {
			case VREG:
			case VDIR:
			case VLNK:
				error = EROFS;
			default:
				break;
			}
		}
		/*
		 * If there's shared text associated with
		 * the inode, try to free it up once.  If
		 * we fail, we can't allow writing.
		 */
		if (VOP_IS_TEXT(vp) && error == 0)
			error = ETXTBSY;
	}
	if (error != 0) {
		if (vpislocked == 0)
			NFSVOPUNLOCK(vp, 0);
		goto out;
	}

	/*
	 * Should the override still be applied when ACLs are enabled?
	 */
	error = VOP_ACCESSX(vp, accmode, cred, p);
	if (error != 0 && (accmode & (VDELETE | VDELETE_CHILD))) {
		/*
		 * Try again with VEXPLICIT_DENY, to see if the test for
		 * deletion is supported.
		 */
		error = VOP_ACCESSX(vp, accmode | VEXPLICIT_DENY, cred, p);
		if (error == 0) {
			if (vp->v_type == VDIR) {
				accmode &= ~(VDELETE | VDELETE_CHILD);
				accmode |= VWRITE;
				error = VOP_ACCESSX(vp, accmode, cred, p);
			} else if (supportedtypep != NULL) {
				*supportedtypep &= ~NFSACCESS_DELETE;
			}
		}
	}

	/*
	 * Allow certain operations for the owner (reads and writes
	 * on files that are already open).
	 */
	if (override != NFSACCCHK_NOOVERRIDE &&
	    (error == EPERM || error == EACCES)) {
		if (cred->cr_uid == 0 && (override & NFSACCCHK_ALLOWROOT))
			error = 0;
		else if (override & NFSACCCHK_ALLOWOWNER) {
			getret = VOP_GETATTR(vp, &vattr, cred);
			if (getret == 0 && cred->cr_uid == vattr.va_uid)
				error = 0;
		}
	}
	if (vpislocked == 0)
		NFSVOPUNLOCK(vp, 0);

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Set attribute(s) vnop.
 */
int
nfsvno_setattr(struct vnode *vp, struct nfsvattr *nvap, struct ucred *cred,
    struct thread *p, struct nfsexstuff *exp)
{
	u_quad_t savsize = 0;
	int error, savedit;

	/*
	 * If this is an exported file system and a pNFS service is running,
	 * don't VOP_SETATTR() of size for the MDS file system.
	 */
	savedit = 0;
	error = 0;
	if (vp->v_type == VREG && (vp->v_mount->mnt_flag & MNT_EXPORTED) != 0 &&
	    nfsrv_devidcnt != 0 && nvap->na_vattr.va_size != VNOVAL &&
	    nvap->na_vattr.va_size > 0) {
		savsize = nvap->na_vattr.va_size;
		nvap->na_vattr.va_size = VNOVAL;
		if (nvap->na_vattr.va_uid != (uid_t)VNOVAL ||
		    nvap->na_vattr.va_gid != (gid_t)VNOVAL ||
		    nvap->na_vattr.va_mode != (mode_t)VNOVAL ||
		    nvap->na_vattr.va_atime.tv_sec != VNOVAL ||
		    nvap->na_vattr.va_mtime.tv_sec != VNOVAL)
			savedit = 1;
		else
			savedit = 2;
	}
	if (savedit != 2)
		error = VOP_SETATTR(vp, &nvap->na_vattr, cred);
	if (savedit != 0)
		nvap->na_vattr.va_size = savsize;
	if (error == 0 && (nvap->na_vattr.va_uid != (uid_t)VNOVAL ||
	    nvap->na_vattr.va_gid != (gid_t)VNOVAL ||
	    nvap->na_vattr.va_size != VNOVAL ||
	    nvap->na_vattr.va_mode != (mode_t)VNOVAL ||
	    nvap->na_vattr.va_atime.tv_sec != VNOVAL ||
	    nvap->na_vattr.va_mtime.tv_sec != VNOVAL)) {
		/* For a pNFS server, set the attributes on the DS file. */
		error = nfsrv_proxyds(NULL, vp, 0, 0, cred, p, NFSPROC_SETATTR,
		    NULL, NULL, NULL, nvap, NULL);
		if (error == ENOENT)
			error = 0;
	}
	NFSEXITCODE(error);
	return (error);
}

/*
 * Set up nameidata for a lookup() call and do it.
 */
int
nfsvno_namei(struct nfsrv_descript *nd, struct nameidata *ndp,
    struct vnode *dp, int islocked, struct nfsexstuff *exp, struct thread *p,
    struct vnode **retdirp)
{
	struct componentname *cnp = &ndp->ni_cnd;
	int i;
	struct iovec aiov;
	struct uio auio;
	int lockleaf = (cnp->cn_flags & LOCKLEAF) != 0, linklen;
	int error = 0;
	char *cp;

	*retdirp = NULL;
	cnp->cn_nameptr = cnp->cn_pnbuf;
	ndp->ni_lcf = 0;
	/*
	 * Extract and set starting directory.
	 */
	if (dp->v_type != VDIR) {
		if (islocked)
			vput(dp);
		else
			vrele(dp);
		nfsvno_relpathbuf(ndp);
		error = ENOTDIR;
		goto out1;
	}
	if (islocked)
		NFSVOPUNLOCK(dp, 0);
	VREF(dp);
	*retdirp = dp;
	if (NFSVNO_EXRDONLY(exp))
		cnp->cn_flags |= RDONLY;
	ndp->ni_segflg = UIO_SYSSPACE;

	if (nd->nd_flag & ND_PUBLOOKUP) {
		ndp->ni_loopcnt = 0;
		if (cnp->cn_pnbuf[0] == '/') {
			vrele(dp);
			/*
			 * Check for degenerate pathnames here, since lookup()
			 * panics on them.
			 */
			for (i = 1; i < ndp->ni_pathlen; i++)
				if (cnp->cn_pnbuf[i] != '/')
					break;
			if (i == ndp->ni_pathlen) {
				error = NFSERR_ACCES;
				goto out;
			}
			dp = rootvnode;
			VREF(dp);
		}
	} else if ((nfsrv_enable_crossmntpt == 0 && NFSVNO_EXPORTED(exp)) ||
	    (nd->nd_flag & ND_NFSV4) == 0) {
		/*
		 * Only cross mount points for NFSv4 when doing a
		 * mount while traversing the file system above
		 * the mount point, unless nfsrv_enable_crossmntpt is set.
		 */
		cnp->cn_flags |= NOCROSSMOUNT;
	}

	/*
	 * Initialize for scan, set ni_startdir and bump ref on dp again
	 * because lookup() will dereference ni_startdir.
	 */

	cnp->cn_thread = p;
	ndp->ni_startdir = dp;
	ndp->ni_rootdir = rootvnode;
	ndp->ni_topdir = NULL;

	if (!lockleaf)
		cnp->cn_flags |= LOCKLEAF;
	for (;;) {
		cnp->cn_nameptr = cnp->cn_pnbuf;
		/*
		 * Call lookup() to do the real work.  If an error occurs,
		 * ndp->ni_vp and ni_dvp are left uninitialized or NULL and
		 * we do not have to dereference anything before returning.
		 * In either case ni_startdir will be dereferenced and NULLed
		 * out.
		 */
		error = lookup(ndp);
		if (error)
			break;

		/*
		 * Check for encountering a symbolic link.  Trivial
		 * termination occurs if no symlink encountered.
		 */
		if ((cnp->cn_flags & ISSYMLINK) == 0) {
			if ((cnp->cn_flags & (SAVENAME | SAVESTART)) == 0)
				nfsvno_relpathbuf(ndp);
			if (ndp->ni_vp && !lockleaf)
				NFSVOPUNLOCK(ndp->ni_vp, 0);
			break;
		}

		/*
		 * Validate symlink
		 */
		if ((cnp->cn_flags & LOCKPARENT) && ndp->ni_pathlen == 1)
			NFSVOPUNLOCK(ndp->ni_dvp, 0);
		if (!(nd->nd_flag & ND_PUBLOOKUP)) {
			error = EINVAL;
			goto badlink2;
		}

		if (ndp->ni_loopcnt++ >= MAXSYMLINKS) {
			error = ELOOP;
			goto badlink2;
		}
		if (ndp->ni_pathlen > 1)
			cp = uma_zalloc(namei_zone, M_WAITOK);
		else
			cp = cnp->cn_pnbuf;
		aiov.iov_base = cp;
		aiov.iov_len = MAXPATHLEN;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_td = NULL;
		auio.uio_resid = MAXPATHLEN;
		error = VOP_READLINK(ndp->ni_vp, &auio, cnp->cn_cred);
		if (error) {
		badlink1:
			if (ndp->ni_pathlen > 1)
				uma_zfree(namei_zone, cp);
		badlink2:
			vrele(ndp->ni_dvp);
			vput(ndp->ni_vp);
			break;
		}
		linklen = MAXPATHLEN - auio.uio_resid;
		if (linklen == 0) {
			error = ENOENT;
			goto badlink1;
		}
		if (linklen + ndp->ni_pathlen >= MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto badlink1;
		}

		/*
		 * Adjust or replace path
		 */
		if (ndp->ni_pathlen > 1) {
			NFSBCOPY(ndp->ni_next, cp + linklen, ndp->ni_pathlen);
			uma_zfree(namei_zone, cnp->cn_pnbuf);
			cnp->cn_pnbuf = cp;
		} else
			cnp->cn_pnbuf[linklen] = '\0';
		ndp->ni_pathlen += linklen;

		/*
		 * Cleanup refs for next loop and check if root directory
		 * should replace current directory.  Normally ni_dvp
		 * becomes the new base directory and is cleaned up when
		 * we loop.  Explicitly null pointers after invalidation
		 * to clarify operation.
		 */
		vput(ndp->ni_vp);
		ndp->ni_vp = NULL;

		if (cnp->cn_pnbuf[0] == '/') {
			vrele(ndp->ni_dvp);
			ndp->ni_dvp = ndp->ni_rootdir;
			VREF(ndp->ni_dvp);
		}
		ndp->ni_startdir = ndp->ni_dvp;
		ndp->ni_dvp = NULL;
	}
	if (!lockleaf)
		cnp->cn_flags &= ~LOCKLEAF;

out:
	if (error) {
		nfsvno_relpathbuf(ndp);
		ndp->ni_vp = NULL;
		ndp->ni_dvp = NULL;
		ndp->ni_startdir = NULL;
	} else if ((ndp->ni_cnd.cn_flags & (WANTPARENT|LOCKPARENT)) == 0) {
		ndp->ni_dvp = NULL;
	}

out1:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Set up a pathname buffer and return a pointer to it and, optionally
 * set a hash pointer.
 */
void
nfsvno_setpathbuf(struct nameidata *ndp, char **bufpp, u_long **hashpp)
{
	struct componentname *cnp = &ndp->ni_cnd;

	cnp->cn_flags |= (NOMACCHECK | HASBUF);
	cnp->cn_pnbuf = uma_zalloc(namei_zone, M_WAITOK);
	if (hashpp != NULL)
		*hashpp = NULL;
	*bufpp = cnp->cn_pnbuf;
}

/*
 * Release the above path buffer, if not released by nfsvno_namei().
 */
void
nfsvno_relpathbuf(struct nameidata *ndp)
{

	if ((ndp->ni_cnd.cn_flags & HASBUF) == 0)
		panic("nfsrelpath");
	uma_zfree(namei_zone, ndp->ni_cnd.cn_pnbuf);
	ndp->ni_cnd.cn_flags &= ~HASBUF;
}

/*
 * Readlink vnode op into an mbuf list.
 */
int
nfsvno_readlink(struct vnode *vp, struct ucred *cred, struct thread *p,
    struct mbuf **mpp, struct mbuf **mpendp, int *lenp)
{
	struct iovec iv[(NFS_MAXPATHLEN+MLEN-1)/MLEN];
	struct iovec *ivp = iv;
	struct uio io, *uiop = &io;
	struct mbuf *mp, *mp2 = NULL, *mp3 = NULL;
	int i, len, tlen, error = 0;

	len = 0;
	i = 0;
	while (len < NFS_MAXPATHLEN) {
		NFSMGET(mp);
		MCLGET(mp, M_WAITOK);
		mp->m_len = M_SIZE(mp);
		if (len == 0) {
			mp3 = mp2 = mp;
		} else {
			mp2->m_next = mp;
			mp2 = mp;
		}
		if ((len + mp->m_len) > NFS_MAXPATHLEN) {
			mp->m_len = NFS_MAXPATHLEN - len;
			len = NFS_MAXPATHLEN;
		} else {
			len += mp->m_len;
		}
		ivp->iov_base = mtod(mp, caddr_t);
		ivp->iov_len = mp->m_len;
		i++;
		ivp++;
	}
	uiop->uio_iov = iv;
	uiop->uio_iovcnt = i;
	uiop->uio_offset = 0;
	uiop->uio_resid = len;
	uiop->uio_rw = UIO_READ;
	uiop->uio_segflg = UIO_SYSSPACE;
	uiop->uio_td = NULL;
	error = VOP_READLINK(vp, uiop, cred);
	if (error) {
		m_freem(mp3);
		*lenp = 0;
		goto out;
	}
	if (uiop->uio_resid > 0) {
		len -= uiop->uio_resid;
		tlen = NFSM_RNDUP(len);
		nfsrv_adj(mp3, NFS_MAXPATHLEN - tlen, tlen - len);
	}
	*lenp = len;
	*mpp = mp3;
	*mpendp = mp;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Read vnode op call into mbuf list.
 */
int
nfsvno_read(struct vnode *vp, off_t off, int cnt, struct ucred *cred,
    struct thread *p, struct mbuf **mpp, struct mbuf **mpendp)
{
	struct mbuf *m;
	int i;
	struct iovec *iv;
	struct iovec *iv2;
	int error = 0, len, left, siz, tlen, ioflag = 0;
	struct mbuf *m2 = NULL, *m3;
	struct uio io, *uiop = &io;
	struct nfsheur *nh;

	/*
	 * Attempt to read from a DS file. A return of ENOENT implies
	 * there is no DS file to read.
	 */
	error = nfsrv_proxyds(NULL, vp, off, cnt, cred, p, NFSPROC_READDS, mpp,
	    NULL, mpendp, NULL, NULL);
	if (error != ENOENT)
		return (error);

	len = left = NFSM_RNDUP(cnt);
	m3 = NULL;
	/*
	 * Generate the mbuf list with the uio_iov ref. to it.
	 */
	i = 0;
	while (left > 0) {
		NFSMGET(m);
		MCLGET(m, M_WAITOK);
		m->m_len = 0;
		siz = min(M_TRAILINGSPACE(m), left);
		left -= siz;
		i++;
		if (m3)
			m2->m_next = m;
		else
			m3 = m;
		m2 = m;
	}
	iv = malloc(i * sizeof (struct iovec),
	    M_TEMP, M_WAITOK);
	uiop->uio_iov = iv2 = iv;
	m = m3;
	left = len;
	i = 0;
	while (left > 0) {
		if (m == NULL)
			panic("nfsvno_read iov");
		siz = min(M_TRAILINGSPACE(m), left);
		if (siz > 0) {
			iv->iov_base = mtod(m, caddr_t) + m->m_len;
			iv->iov_len = siz;
			m->m_len += siz;
			left -= siz;
			iv++;
			i++;
		}
		m = m->m_next;
	}
	uiop->uio_iovcnt = i;
	uiop->uio_offset = off;
	uiop->uio_resid = len;
	uiop->uio_rw = UIO_READ;
	uiop->uio_segflg = UIO_SYSSPACE;
	uiop->uio_td = NULL;
	nh = nfsrv_sequential_heuristic(uiop, vp);
	ioflag |= nh->nh_seqcount << IO_SEQSHIFT;
	/* XXX KDM make this more systematic? */
	nfsstatsv1.srvbytes[NFSV4OP_READ] += uiop->uio_resid;
	error = VOP_READ(vp, uiop, IO_NODELOCKED | ioflag, cred);
	free(iv2, M_TEMP);
	if (error) {
		m_freem(m3);
		*mpp = NULL;
		goto out;
	}
	nh->nh_nextoff = uiop->uio_offset;
	tlen = len - uiop->uio_resid;
	cnt = cnt < tlen ? cnt : tlen;
	tlen = NFSM_RNDUP(cnt);
	if (tlen == 0) {
		m_freem(m3);
		m3 = NULL;
	} else if (len != tlen || tlen != cnt)
		nfsrv_adj(m3, len - tlen, tlen - cnt);
	*mpp = m3;
	*mpendp = m2;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Write vnode op from an mbuf list.
 */
int
nfsvno_write(struct vnode *vp, off_t off, int retlen, int cnt, int *stable,
    struct mbuf *mp, char *cp, struct ucred *cred, struct thread *p)
{
	struct iovec *ivp;
	int i, len;
	struct iovec *iv;
	int ioflags, error;
	struct uio io, *uiop = &io;
	struct nfsheur *nh;

	/*
	 * Attempt to write to a DS file. A return of ENOENT implies
	 * there is no DS file to write.
	 */
	error = nfsrv_proxyds(NULL, vp, off, retlen, cred, p, NFSPROC_WRITEDS,
	    &mp, cp, NULL, NULL, NULL);
	if (error != ENOENT) {
		*stable = NFSWRITE_FILESYNC;
		return (error);
	}

	ivp = malloc(cnt * sizeof (struct iovec), M_TEMP,
	    M_WAITOK);
	uiop->uio_iov = iv = ivp;
	uiop->uio_iovcnt = cnt;
	i = mtod(mp, caddr_t) + mp->m_len - cp;
	len = retlen;
	while (len > 0) {
		if (mp == NULL)
			panic("nfsvno_write");
		if (i > 0) {
			i = min(i, len);
			ivp->iov_base = cp;
			ivp->iov_len = i;
			ivp++;
			len -= i;
		}
		mp = mp->m_next;
		if (mp) {
			i = mp->m_len;
			cp = mtod(mp, caddr_t);
		}
	}

	if (*stable == NFSWRITE_UNSTABLE)
		ioflags = IO_NODELOCKED;
	else
		ioflags = (IO_SYNC | IO_NODELOCKED);
	uiop->uio_resid = retlen;
	uiop->uio_rw = UIO_WRITE;
	uiop->uio_segflg = UIO_SYSSPACE;
	NFSUIOPROC(uiop, p);
	uiop->uio_offset = off;
	nh = nfsrv_sequential_heuristic(uiop, vp);
	ioflags |= nh->nh_seqcount << IO_SEQSHIFT;
	/* XXX KDM make this more systematic? */
	nfsstatsv1.srvbytes[NFSV4OP_WRITE] += uiop->uio_resid;
	error = VOP_WRITE(vp, uiop, ioflags, cred);
	if (error == 0)
		nh->nh_nextoff = uiop->uio_offset;
	free(iv, M_TEMP);

	NFSEXITCODE(error);
	return (error);
}

/*
 * Common code for creating a regular file (plus special files for V2).
 */
int
nfsvno_createsub(struct nfsrv_descript *nd, struct nameidata *ndp,
    struct vnode **vpp, struct nfsvattr *nvap, int *exclusive_flagp,
    int32_t *cverf, NFSDEV_T rdev, struct nfsexstuff *exp)
{
	u_quad_t tempsize;
	int error;
	struct thread *p = curthread;

	error = nd->nd_repstat;
	if (!error && ndp->ni_vp == NULL) {
		if (nvap->na_type == VREG || nvap->na_type == VSOCK) {
			vrele(ndp->ni_startdir);
			error = VOP_CREATE(ndp->ni_dvp,
			    &ndp->ni_vp, &ndp->ni_cnd, &nvap->na_vattr);
			/* For a pNFS server, create the data file on a DS. */
			if (error == 0 && nvap->na_type == VREG) {
				/*
				 * Create a data file on a DS for a pNFS server.
				 * This function just returns if not
				 * running a pNFS DS or the creation fails.
				 */
				nfsrv_pnfscreate(ndp->ni_vp, &nvap->na_vattr,
				    nd->nd_cred, p);
			}
			vput(ndp->ni_dvp);
			nfsvno_relpathbuf(ndp);
			if (!error) {
				if (*exclusive_flagp) {
					*exclusive_flagp = 0;
					NFSVNO_ATTRINIT(nvap);
					nvap->na_atime.tv_sec = cverf[0];
					nvap->na_atime.tv_nsec = cverf[1];
					error = VOP_SETATTR(ndp->ni_vp,
					    &nvap->na_vattr, nd->nd_cred);
					if (error != 0) {
						vput(ndp->ni_vp);
						ndp->ni_vp = NULL;
						error = NFSERR_NOTSUPP;
					}
				}
			}
		/*
		 * NFS V2 Only. nfsrvd_mknod() does this for V3.
		 * (This implies, just get out on an error.)
		 */
		} else if (nvap->na_type == VCHR || nvap->na_type == VBLK ||
			nvap->na_type == VFIFO) {
			if (nvap->na_type == VCHR && rdev == 0xffffffff)
				nvap->na_type = VFIFO;
                        if (nvap->na_type != VFIFO &&
			    (error = priv_check_cred(nd->nd_cred, PRIV_VFS_MKNOD_DEV))) {
				vrele(ndp->ni_startdir);
				nfsvno_relpathbuf(ndp);
				vput(ndp->ni_dvp);
				goto out;
			}
			nvap->na_rdev = rdev;
			error = VOP_MKNOD(ndp->ni_dvp, &ndp->ni_vp,
			    &ndp->ni_cnd, &nvap->na_vattr);
			vput(ndp->ni_dvp);
			nfsvno_relpathbuf(ndp);
			vrele(ndp->ni_startdir);
			if (error)
				goto out;
		} else {
			vrele(ndp->ni_startdir);
			nfsvno_relpathbuf(ndp);
			vput(ndp->ni_dvp);
			error = ENXIO;
			goto out;
		}
		*vpp = ndp->ni_vp;
	} else {
		/*
		 * Handle cases where error is already set and/or
		 * the file exists.
		 * 1 - clean up the lookup
		 * 2 - iff !error and na_size set, truncate it
		 */
		vrele(ndp->ni_startdir);
		nfsvno_relpathbuf(ndp);
		*vpp = ndp->ni_vp;
		if (ndp->ni_dvp == *vpp)
			vrele(ndp->ni_dvp);
		else
			vput(ndp->ni_dvp);
		if (!error && nvap->na_size != VNOVAL) {
			error = nfsvno_accchk(*vpp, VWRITE,
			    nd->nd_cred, exp, p, NFSACCCHK_NOOVERRIDE,
			    NFSACCCHK_VPISLOCKED, NULL);
			if (!error) {
				tempsize = nvap->na_size;
				NFSVNO_ATTRINIT(nvap);
				nvap->na_size = tempsize;
				error = VOP_SETATTR(*vpp,
				    &nvap->na_vattr, nd->nd_cred);
			}
		}
		if (error)
			vput(*vpp);
	}

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Do a mknod vnode op.
 */
int
nfsvno_mknod(struct nameidata *ndp, struct nfsvattr *nvap, struct ucred *cred,
    struct thread *p)
{
	int error = 0;
	enum vtype vtyp;

	vtyp = nvap->na_type;
	/*
	 * Iff doesn't exist, create it.
	 */
	if (ndp->ni_vp) {
		vrele(ndp->ni_startdir);
		nfsvno_relpathbuf(ndp);
		vput(ndp->ni_dvp);
		vrele(ndp->ni_vp);
		error = EEXIST;
		goto out;
	}
	if (vtyp != VCHR && vtyp != VBLK && vtyp != VSOCK && vtyp != VFIFO) {
		vrele(ndp->ni_startdir);
		nfsvno_relpathbuf(ndp);
		vput(ndp->ni_dvp);
		error = NFSERR_BADTYPE;
		goto out;
	}
	if (vtyp == VSOCK) {
		vrele(ndp->ni_startdir);
		error = VOP_CREATE(ndp->ni_dvp, &ndp->ni_vp,
		    &ndp->ni_cnd, &nvap->na_vattr);
		vput(ndp->ni_dvp);
		nfsvno_relpathbuf(ndp);
	} else {
		if (nvap->na_type != VFIFO &&
		    (error = priv_check_cred(cred, PRIV_VFS_MKNOD_DEV))) {
			vrele(ndp->ni_startdir);
			nfsvno_relpathbuf(ndp);
			vput(ndp->ni_dvp);
			goto out;
		}
		error = VOP_MKNOD(ndp->ni_dvp, &ndp->ni_vp,
		    &ndp->ni_cnd, &nvap->na_vattr);
		vput(ndp->ni_dvp);
		nfsvno_relpathbuf(ndp);
		vrele(ndp->ni_startdir);
		/*
		 * Since VOP_MKNOD returns the ni_vp, I can't
		 * see any reason to do the lookup.
		 */
	}

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Mkdir vnode op.
 */
int
nfsvno_mkdir(struct nameidata *ndp, struct nfsvattr *nvap, uid_t saved_uid,
    struct ucred *cred, struct thread *p, struct nfsexstuff *exp)
{
	int error = 0;

	if (ndp->ni_vp != NULL) {
		if (ndp->ni_dvp == ndp->ni_vp)
			vrele(ndp->ni_dvp);
		else
			vput(ndp->ni_dvp);
		vrele(ndp->ni_vp);
		nfsvno_relpathbuf(ndp);
		error = EEXIST;
		goto out;
	}
	error = VOP_MKDIR(ndp->ni_dvp, &ndp->ni_vp, &ndp->ni_cnd,
	    &nvap->na_vattr);
	vput(ndp->ni_dvp);
	nfsvno_relpathbuf(ndp);

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * symlink vnode op.
 */
int
nfsvno_symlink(struct nameidata *ndp, struct nfsvattr *nvap, char *pathcp,
    int pathlen, int not_v2, uid_t saved_uid, struct ucred *cred, struct thread *p,
    struct nfsexstuff *exp)
{
	int error = 0;

	if (ndp->ni_vp) {
		vrele(ndp->ni_startdir);
		nfsvno_relpathbuf(ndp);
		if (ndp->ni_dvp == ndp->ni_vp)
			vrele(ndp->ni_dvp);
		else
			vput(ndp->ni_dvp);
		vrele(ndp->ni_vp);
		error = EEXIST;
		goto out;
	}

	error = VOP_SYMLINK(ndp->ni_dvp, &ndp->ni_vp, &ndp->ni_cnd,
	    &nvap->na_vattr, pathcp);
	vput(ndp->ni_dvp);
	vrele(ndp->ni_startdir);
	nfsvno_relpathbuf(ndp);
	/*
	 * Although FreeBSD still had the lookup code in
	 * it for 7/current, there doesn't seem to be any
	 * point, since VOP_SYMLINK() returns the ni_vp.
	 * Just vput it for v2.
	 */
	if (!not_v2 && !error)
		vput(ndp->ni_vp);

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Parse symbolic link arguments.
 * This function has an ugly side effect. It will malloc() an area for
 * the symlink and set iov_base to point to it, only if it succeeds.
 * So, if it returns with uiop->uio_iov->iov_base != NULL, that must
 * be FREE'd later.
 */
int
nfsvno_getsymlink(struct nfsrv_descript *nd, struct nfsvattr *nvap,
    struct thread *p, char **pathcpp, int *lenp)
{
	u_int32_t *tl;
	char *pathcp = NULL;
	int error = 0, len;
	struct nfsv2_sattr *sp;

	*pathcpp = NULL;
	*lenp = 0;
	if ((nd->nd_flag & ND_NFSV3) &&
	    (error = nfsrv_sattr(nd, NULL, nvap, NULL, NULL, p)))
		goto nfsmout;
	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	len = fxdr_unsigned(int, *tl);
	if (len > NFS_MAXPATHLEN || len <= 0) {
		error = EBADRPC;
		goto nfsmout;
	}
	pathcp = malloc(len + 1, M_TEMP, M_WAITOK);
	error = nfsrv_mtostr(nd, pathcp, len);
	if (error)
		goto nfsmout;
	if (nd->nd_flag & ND_NFSV2) {
		NFSM_DISSECT(sp, struct nfsv2_sattr *, NFSX_V2SATTR);
		nvap->na_mode = fxdr_unsigned(u_int16_t, sp->sa_mode);
	}
	*pathcpp = pathcp;
	*lenp = len;
	NFSEXITCODE2(0, nd);
	return (0);
nfsmout:
	if (pathcp)
		free(pathcp, M_TEMP);
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Remove a non-directory object.
 */
int
nfsvno_removesub(struct nameidata *ndp, int is_v4, struct ucred *cred,
    struct thread *p, struct nfsexstuff *exp)
{
	struct vnode *vp, *dsdvp[NFSDEV_MAXMIRRORS];
	int error = 0, mirrorcnt;
	char fname[PNFS_FILENAME_LEN + 1];
	fhandle_t fh;

	vp = ndp->ni_vp;
	dsdvp[0] = NULL;
	if (vp->v_type == VDIR)
		error = NFSERR_ISDIR;
	else if (is_v4)
		error = nfsrv_checkremove(vp, 1, p);
	if (error == 0)
		nfsrv_pnfsremovesetup(vp, p, dsdvp, &mirrorcnt, fname, &fh);
	if (!error)
		error = VOP_REMOVE(ndp->ni_dvp, vp, &ndp->ni_cnd);
	if (error == 0 && dsdvp[0] != NULL)
		nfsrv_pnfsremove(dsdvp, mirrorcnt, fname, &fh, p);
	if (ndp->ni_dvp == vp)
		vrele(ndp->ni_dvp);
	else
		vput(ndp->ni_dvp);
	vput(vp);
	if ((ndp->ni_cnd.cn_flags & SAVENAME) != 0)
		nfsvno_relpathbuf(ndp);
	NFSEXITCODE(error);
	return (error);
}

/*
 * Remove a directory.
 */
int
nfsvno_rmdirsub(struct nameidata *ndp, int is_v4, struct ucred *cred,
    struct thread *p, struct nfsexstuff *exp)
{
	struct vnode *vp;
	int error = 0;

	vp = ndp->ni_vp;
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}
	/*
	 * No rmdir "." please.
	 */
	if (ndp->ni_dvp == vp) {
		error = EINVAL;
		goto out;
	}
	/*
	 * The root of a mounted filesystem cannot be deleted.
	 */
	if (vp->v_vflag & VV_ROOT)
		error = EBUSY;
out:
	if (!error)
		error = VOP_RMDIR(ndp->ni_dvp, vp, &ndp->ni_cnd);
	if (ndp->ni_dvp == vp)
		vrele(ndp->ni_dvp);
	else
		vput(ndp->ni_dvp);
	vput(vp);
	if ((ndp->ni_cnd.cn_flags & SAVENAME) != 0)
		nfsvno_relpathbuf(ndp);
	NFSEXITCODE(error);
	return (error);
}

/*
 * Rename vnode op.
 */
int
nfsvno_rename(struct nameidata *fromndp, struct nameidata *tondp,
    u_int32_t ndstat, u_int32_t ndflag, struct ucred *cred, struct thread *p)
{
	struct vnode *fvp, *tvp, *tdvp, *dsdvp[NFSDEV_MAXMIRRORS];
	int error = 0, mirrorcnt;
	char fname[PNFS_FILENAME_LEN + 1];
	fhandle_t fh;

	dsdvp[0] = NULL;
	fvp = fromndp->ni_vp;
	if (ndstat) {
		vrele(fromndp->ni_dvp);
		vrele(fvp);
		error = ndstat;
		goto out1;
	}
	tdvp = tondp->ni_dvp;
	tvp = tondp->ni_vp;
	if (tvp != NULL) {
		if (fvp->v_type == VDIR && tvp->v_type != VDIR) {
			error = (ndflag & ND_NFSV2) ? EISDIR : EEXIST;
			goto out;
		} else if (fvp->v_type != VDIR && tvp->v_type == VDIR) {
			error = (ndflag & ND_NFSV2) ? ENOTDIR : EEXIST;
			goto out;
		}
		if (tvp->v_type == VDIR && tvp->v_mountedhere) {
			error = (ndflag & ND_NFSV2) ? ENOTEMPTY : EXDEV;
			goto out;
		}

		/*
		 * A rename to '.' or '..' results in a prematurely
		 * unlocked vnode on FreeBSD5, so I'm just going to fail that
		 * here.
		 */
		if ((tondp->ni_cnd.cn_namelen == 1 &&
		     tondp->ni_cnd.cn_nameptr[0] == '.') ||
		    (tondp->ni_cnd.cn_namelen == 2 &&
		     tondp->ni_cnd.cn_nameptr[0] == '.' &&
		     tondp->ni_cnd.cn_nameptr[1] == '.')) {
			error = EINVAL;
			goto out;
		}
	}
	if (fvp->v_type == VDIR && fvp->v_mountedhere) {
		error = (ndflag & ND_NFSV2) ? ENOTEMPTY : EXDEV;
		goto out;
	}
	if (fvp->v_mount != tdvp->v_mount) {
		error = (ndflag & ND_NFSV2) ? ENOTEMPTY : EXDEV;
		goto out;
	}
	if (fvp == tdvp) {
		error = (ndflag & ND_NFSV2) ? ENOTEMPTY : EINVAL;
		goto out;
	}
	if (fvp == tvp) {
		/*
		 * If source and destination are the same, there is nothing to
		 * do. Set error to -1 to indicate this.
		 */
		error = -1;
		goto out;
	}
	if (ndflag & ND_NFSV4) {
		if (NFSVOPLOCK(fvp, LK_EXCLUSIVE) == 0) {
			error = nfsrv_checkremove(fvp, 0, p);
			NFSVOPUNLOCK(fvp, 0);
		} else
			error = EPERM;
		if (tvp && !error)
			error = nfsrv_checkremove(tvp, 1, p);
	} else {
		/*
		 * For NFSv2 and NFSv3, try to get rid of the delegation, so
		 * that the NFSv4 client won't be confused by the rename.
		 * Since nfsd_recalldelegation() can only be called on an
		 * unlocked vnode at this point and fvp is the file that will
		 * still exist after the rename, just do fvp.
		 */
		nfsd_recalldelegation(fvp, p);
	}
	if (error == 0 && tvp != NULL) {
		nfsrv_pnfsremovesetup(tvp, p, dsdvp, &mirrorcnt, fname, &fh);
		NFSD_DEBUG(4, "nfsvno_rename: pnfsremovesetup"
		    " dsdvp=%p\n", dsdvp[0]);
	}
out:
	if (!error) {
		error = VOP_RENAME(fromndp->ni_dvp, fromndp->ni_vp,
		    &fromndp->ni_cnd, tondp->ni_dvp, tondp->ni_vp,
		    &tondp->ni_cnd);
	} else {
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		vrele(fromndp->ni_dvp);
		vrele(fvp);
		if (error == -1)
			error = 0;
	}

	/*
	 * If dsdvp[0] != NULL, it was set up by nfsrv_pnfsremovesetup() and
	 * if the rename succeeded, the DS file for the tvp needs to be
	 * removed.
	 */
	if (error == 0 && dsdvp[0] != NULL) {
		nfsrv_pnfsremove(dsdvp, mirrorcnt, fname, &fh, p);
		NFSD_DEBUG(4, "nfsvno_rename: pnfsremove\n");
	}

	vrele(tondp->ni_startdir);
	nfsvno_relpathbuf(tondp);
out1:
	vrele(fromndp->ni_startdir);
	nfsvno_relpathbuf(fromndp);
	NFSEXITCODE(error);
	return (error);
}

/*
 * Link vnode op.
 */
int
nfsvno_link(struct nameidata *ndp, struct vnode *vp, struct ucred *cred,
    struct thread *p, struct nfsexstuff *exp)
{
	struct vnode *xp;
	int error = 0;

	xp = ndp->ni_vp;
	if (xp != NULL) {
		error = EEXIST;
	} else {
		xp = ndp->ni_dvp;
		if (vp->v_mount != xp->v_mount)
			error = EXDEV;
	}
	if (!error) {
		NFSVOPLOCK(vp, LK_EXCLUSIVE | LK_RETRY);
		if ((vp->v_iflag & VI_DOOMED) == 0)
			error = VOP_LINK(ndp->ni_dvp, vp, &ndp->ni_cnd);
		else
			error = EPERM;
		if (ndp->ni_dvp == vp)
			vrele(ndp->ni_dvp);
		else
			vput(ndp->ni_dvp);
		NFSVOPUNLOCK(vp, 0);
	} else {
		if (ndp->ni_dvp == ndp->ni_vp)
			vrele(ndp->ni_dvp);
		else
			vput(ndp->ni_dvp);
		if (ndp->ni_vp)
			vrele(ndp->ni_vp);
	}
	nfsvno_relpathbuf(ndp);
	NFSEXITCODE(error);
	return (error);
}

/*
 * Do the fsync() appropriate for the commit.
 */
int
nfsvno_fsync(struct vnode *vp, u_int64_t off, int cnt, struct ucred *cred,
    struct thread *td)
{
	int error = 0;

	/*
	 * RFC 1813 3.3.21: if count is 0, a flush from offset to the end of
	 * file is done.  At this time VOP_FSYNC does not accept offset and
	 * byte count parameters so call VOP_FSYNC the whole file for now.
	 * The same is true for NFSv4: RFC 3530 Sec. 14.2.3.
	 * File systems that do not use the buffer cache (as indicated
	 * by MNTK_USES_BCACHE not being set) must use VOP_FSYNC().
	 */
	if (cnt == 0 || cnt > MAX_COMMIT_COUNT ||
	    (vp->v_mount->mnt_kern_flag & MNTK_USES_BCACHE) == 0) {
		/*
		 * Give up and do the whole thing
		 */
		if (vp->v_object &&
		   (vp->v_object->flags & OBJ_MIGHTBEDIRTY)) {
			VM_OBJECT_WLOCK(vp->v_object);
			vm_object_page_clean(vp->v_object, 0, 0, OBJPC_SYNC);
			VM_OBJECT_WUNLOCK(vp->v_object);
		}
		error = VOP_FSYNC(vp, MNT_WAIT, td);
	} else {
		/*
		 * Locate and synchronously write any buffers that fall
		 * into the requested range.  Note:  we are assuming that
		 * f_iosize is a power of 2.
		 */
		int iosize = vp->v_mount->mnt_stat.f_iosize;
		int iomask = iosize - 1;
		struct bufobj *bo;
		daddr_t lblkno;

		/*
		 * Align to iosize boundary, super-align to page boundary.
		 */
		if (off & iomask) {
			cnt += off & iomask;
			off &= ~(u_quad_t)iomask;
		}
		if (off & PAGE_MASK) {
			cnt += off & PAGE_MASK;
			off &= ~(u_quad_t)PAGE_MASK;
		}
		lblkno = off / iosize;

		if (vp->v_object &&
		   (vp->v_object->flags & OBJ_MIGHTBEDIRTY)) {
			VM_OBJECT_WLOCK(vp->v_object);
			vm_object_page_clean(vp->v_object, off, off + cnt,
			    OBJPC_SYNC);
			VM_OBJECT_WUNLOCK(vp->v_object);
		}

		bo = &vp->v_bufobj;
		BO_LOCK(bo);
		while (cnt > 0) {
			struct buf *bp;

			/*
			 * If we have a buffer and it is marked B_DELWRI we
			 * have to lock and write it.  Otherwise the prior
			 * write is assumed to have already been committed.
			 *
			 * gbincore() can return invalid buffers now so we
			 * have to check that bit as well (though B_DELWRI
			 * should not be set if B_INVAL is set there could be
			 * a race here since we haven't locked the buffer).
			 */
			if ((bp = gbincore(&vp->v_bufobj, lblkno)) != NULL) {
				if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_SLEEPFAIL |
				    LK_INTERLOCK, BO_LOCKPTR(bo)) == ENOLCK) {
					BO_LOCK(bo);
					continue; /* retry */
				}
			    	if ((bp->b_flags & (B_DELWRI|B_INVAL)) ==
				    B_DELWRI) {
					bremfree(bp);
					bp->b_flags &= ~B_ASYNC;
					bwrite(bp);
					++nfs_commit_miss;
				} else
					BUF_UNLOCK(bp);
				BO_LOCK(bo);
			}
			++nfs_commit_blks;
			if (cnt < iosize)
				break;
			cnt -= iosize;
			++lblkno;
		}
		BO_UNLOCK(bo);
	}
	NFSEXITCODE(error);
	return (error);
}

/*
 * Statfs vnode op.
 */
int
nfsvno_statfs(struct vnode *vp, struct statfs *sf)
{
	struct statfs *tsf;
	int error;

	tsf = NULL;
	if (nfsrv_devidcnt > 0) {
		/* For a pNFS service, get the DS numbers. */
		tsf = malloc(sizeof(*tsf), M_TEMP, M_WAITOK | M_ZERO);
		error = nfsrv_pnfsstatfs(tsf, vp->v_mount);
		if (error != 0) {
			free(tsf, M_TEMP);
			tsf = NULL;
		}
	}
	error = VFS_STATFS(vp->v_mount, sf);
	if (error == 0) {
		if (tsf != NULL) {
			sf->f_blocks = tsf->f_blocks;
			sf->f_bavail = tsf->f_bavail;
			sf->f_bfree = tsf->f_bfree;
			sf->f_bsize = tsf->f_bsize;
		}
		/*
		 * Since NFS handles these values as unsigned on the
		 * wire, there is no way to represent negative values,
		 * so set them to 0. Without this, they will appear
		 * to be very large positive values for clients like
		 * Solaris10.
		 */
		if (sf->f_bavail < 0)
			sf->f_bavail = 0;
		if (sf->f_ffree < 0)
			sf->f_ffree = 0;
	}
	free(tsf, M_TEMP);
	NFSEXITCODE(error);
	return (error);
}

/*
 * Do the vnode op stuff for Open. Similar to nfsvno_createsub(), but
 * must handle nfsrv_opencheck() calls after any other access checks.
 */
void
nfsvno_open(struct nfsrv_descript *nd, struct nameidata *ndp,
    nfsquad_t clientid, nfsv4stateid_t *stateidp, struct nfsstate *stp,
    int *exclusive_flagp, struct nfsvattr *nvap, int32_t *cverf, int create,
    NFSACL_T *aclp, nfsattrbit_t *attrbitp, struct ucred *cred,
    struct nfsexstuff *exp, struct vnode **vpp)
{
	struct vnode *vp = NULL;
	u_quad_t tempsize;
	struct nfsexstuff nes;
	struct thread *p = curthread;

	if (ndp->ni_vp == NULL)
		nd->nd_repstat = nfsrv_opencheck(clientid,
		    stateidp, stp, NULL, nd, p, nd->nd_repstat);
	if (!nd->nd_repstat) {
		if (ndp->ni_vp == NULL) {
			vrele(ndp->ni_startdir);
			nd->nd_repstat = VOP_CREATE(ndp->ni_dvp,
			    &ndp->ni_vp, &ndp->ni_cnd, &nvap->na_vattr);
			/* For a pNFS server, create the data file on a DS. */
			if (nd->nd_repstat == 0) {
				/*
				 * Create a data file on a DS for a pNFS server.
				 * This function just returns if not
				 * running a pNFS DS or the creation fails.
				 */
				nfsrv_pnfscreate(ndp->ni_vp, &nvap->na_vattr,
				    cred, p);
			}
			vput(ndp->ni_dvp);
			nfsvno_relpathbuf(ndp);
			if (!nd->nd_repstat) {
				if (*exclusive_flagp) {
					*exclusive_flagp = 0;
					NFSVNO_ATTRINIT(nvap);
					nvap->na_atime.tv_sec = cverf[0];
					nvap->na_atime.tv_nsec = cverf[1];
					nd->nd_repstat = VOP_SETATTR(ndp->ni_vp,
					    &nvap->na_vattr, cred);
					if (nd->nd_repstat != 0) {
						vput(ndp->ni_vp);
						ndp->ni_vp = NULL;
						nd->nd_repstat = NFSERR_NOTSUPP;
					} else
						NFSSETBIT_ATTRBIT(attrbitp,
						    NFSATTRBIT_TIMEACCESS);
				} else {
					nfsrv_fixattr(nd, ndp->ni_vp, nvap,
					    aclp, p, attrbitp, exp);
				}
			}
			vp = ndp->ni_vp;
		} else {
			if (ndp->ni_startdir)
				vrele(ndp->ni_startdir);
			nfsvno_relpathbuf(ndp);
			vp = ndp->ni_vp;
			if (create == NFSV4OPEN_CREATE) {
				if (ndp->ni_dvp == vp)
					vrele(ndp->ni_dvp);
				else
					vput(ndp->ni_dvp);
			}
			if (NFSVNO_ISSETSIZE(nvap) && vp->v_type == VREG) {
				if (ndp->ni_cnd.cn_flags & RDONLY)
					NFSVNO_SETEXRDONLY(&nes);
				else
					NFSVNO_EXINIT(&nes);
				nd->nd_repstat = nfsvno_accchk(vp, 
				    VWRITE, cred, &nes, p,
				    NFSACCCHK_NOOVERRIDE,
				    NFSACCCHK_VPISLOCKED, NULL);
				nd->nd_repstat = nfsrv_opencheck(clientid,
				    stateidp, stp, vp, nd, p, nd->nd_repstat);
				if (!nd->nd_repstat) {
					tempsize = nvap->na_size;
					NFSVNO_ATTRINIT(nvap);
					nvap->na_size = tempsize;
					nd->nd_repstat = VOP_SETATTR(vp,
					    &nvap->na_vattr, cred);
				}
			} else if (vp->v_type == VREG) {
				nd->nd_repstat = nfsrv_opencheck(clientid,
				    stateidp, stp, vp, nd, p, nd->nd_repstat);
			}
		}
	} else {
		if (ndp->ni_cnd.cn_flags & HASBUF)
			nfsvno_relpathbuf(ndp);
		if (ndp->ni_startdir && create == NFSV4OPEN_CREATE) {
			vrele(ndp->ni_startdir);
			if (ndp->ni_dvp == ndp->ni_vp)
				vrele(ndp->ni_dvp);
			else
				vput(ndp->ni_dvp);
			if (ndp->ni_vp)
				vput(ndp->ni_vp);
		}
	}
	*vpp = vp;

	NFSEXITCODE2(0, nd);
}

/*
 * Updates the file rev and sets the mtime and ctime
 * to the current clock time, returning the va_filerev and va_Xtime
 * values.
 * Return ESTALE to indicate the vnode is VI_DOOMED.
 */
int
nfsvno_updfilerev(struct vnode *vp, struct nfsvattr *nvap,
    struct nfsrv_descript *nd, struct thread *p)
{
	struct vattr va;

	VATTR_NULL(&va);
	vfs_timestamp(&va.va_mtime);
	if (NFSVOPISLOCKED(vp) != LK_EXCLUSIVE) {
		NFSVOPLOCK(vp, LK_UPGRADE | LK_RETRY);
		if ((vp->v_iflag & VI_DOOMED) != 0)
			return (ESTALE);
	}
	(void) VOP_SETATTR(vp, &va, nd->nd_cred);
	(void) nfsvno_getattr(vp, nvap, nd, p, 1, NULL);
	return (0);
}

/*
 * Glue routine to nfsv4_fillattr().
 */
int
nfsvno_fillattr(struct nfsrv_descript *nd, struct mount *mp, struct vnode *vp,
    struct nfsvattr *nvap, fhandle_t *fhp, int rderror, nfsattrbit_t *attrbitp,
    struct ucred *cred, struct thread *p, int isdgram, int reterr,
    int supports_nfsv4acls, int at_root, uint64_t mounted_on_fileno)
{
	struct statfs *sf;
	int error;

	sf = NULL;
	if (nfsrv_devidcnt > 0 &&
	    (NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_SPACEAVAIL) ||
	     NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_SPACEFREE) ||
	     NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_SPACETOTAL))) {
		sf = malloc(sizeof(*sf), M_TEMP, M_WAITOK | M_ZERO);
		error = nfsrv_pnfsstatfs(sf, mp);
		if (error != 0) {
			free(sf, M_TEMP);
			sf = NULL;
		}
	}
	error = nfsv4_fillattr(nd, mp, vp, NULL, &nvap->na_vattr, fhp, rderror,
	    attrbitp, cred, p, isdgram, reterr, supports_nfsv4acls, at_root,
	    mounted_on_fileno, sf);
	free(sf, M_TEMP);
	NFSEXITCODE2(0, nd);
	return (error);
}

/* Since the Readdir vnode ops vary, put the entire functions in here. */
/*
 * nfs readdir service
 * - mallocs what it thinks is enough to read
 *	count rounded up to a multiple of DIRBLKSIZ <= NFS_MAXREADDIR
 * - calls VOP_READDIR()
 * - loops around building the reply
 *	if the output generated exceeds count break out of loop
 *	The NFSM_CLGET macro is used here so that the reply will be packed
 *	tightly in mbuf clusters.
 * - it trims out records with d_fileno == 0
 *	this doesn't matter for Unix clients, but they might confuse clients
 *	for other os'.
 * - it trims out records with d_type == DT_WHT
 *	these cannot be seen through NFS (unless we extend the protocol)
 *     The alternate call nfsrvd_readdirplus() does lookups as well.
 * PS: The NFS protocol spec. does not clarify what the "count" byte
 *	argument is a count of.. just name strings and file id's or the
 *	entire reply rpc or ...
 *	I tried just file name and id sizes and it confused the Sun client,
 *	so I am using the full rpc size now. The "paranoia.." comment refers
 *	to including the status longwords that are not a part of the dir.
 *	"entry" structures, but are in the rpc.
 */
int
nfsrvd_readdir(struct nfsrv_descript *nd, int isdgram,
    struct vnode *vp, struct nfsexstuff *exp)
{
	struct dirent *dp;
	u_int32_t *tl;
	int dirlen;
	char *cpos, *cend, *rbuf;
	struct nfsvattr at;
	int nlen, error = 0, getret = 1;
	int siz, cnt, fullsiz, eofflag, ncookies;
	u_int64_t off, toff, verf __unused;
	u_long *cookies = NULL, *cookiep;
	struct uio io;
	struct iovec iv;
	int is_ufs;
	struct thread *p = curthread;

	if (nd->nd_repstat) {
		nfsrv_postopattr(nd, getret, &at);
		goto out;
	}
	if (nd->nd_flag & ND_NFSV2) {
		NFSM_DISSECT(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
		off = fxdr_unsigned(u_quad_t, *tl++);
	} else {
		NFSM_DISSECT(tl, u_int32_t *, 5 * NFSX_UNSIGNED);
		off = fxdr_hyper(tl);
		tl += 2;
		verf = fxdr_hyper(tl);
		tl += 2;
	}
	toff = off;
	cnt = fxdr_unsigned(int, *tl);
	if (cnt > NFS_SRVMAXDATA(nd) || cnt < 0)
		cnt = NFS_SRVMAXDATA(nd);
	siz = ((cnt + DIRBLKSIZ - 1) & ~(DIRBLKSIZ - 1));
	fullsiz = siz;
	if (nd->nd_flag & ND_NFSV3) {
		nd->nd_repstat = getret = nfsvno_getattr(vp, &at, nd, p, 1,
		    NULL);
#if 0
		/*
		 * va_filerev is not sufficient as a cookie verifier,
		 * since it is not supposed to change when entries are
		 * removed/added unless that offset cookies returned to
		 * the client are no longer valid.
		 */
		if (!nd->nd_repstat && toff && verf != at.na_filerev)
			nd->nd_repstat = NFSERR_BAD_COOKIE;
#endif
	}
	if (!nd->nd_repstat && vp->v_type != VDIR)
		nd->nd_repstat = NFSERR_NOTDIR;
	if (nd->nd_repstat == 0 && cnt == 0) {
		if (nd->nd_flag & ND_NFSV2)
			/* NFSv2 does not have NFSERR_TOOSMALL */
			nd->nd_repstat = EPERM;
		else
			nd->nd_repstat = NFSERR_TOOSMALL;
	}
	if (!nd->nd_repstat)
		nd->nd_repstat = nfsvno_accchk(vp, VEXEC,
		    nd->nd_cred, exp, p, NFSACCCHK_NOOVERRIDE,
		    NFSACCCHK_VPISLOCKED, NULL);
	if (nd->nd_repstat) {
		vput(vp);
		if (nd->nd_flag & ND_NFSV3)
			nfsrv_postopattr(nd, getret, &at);
		goto out;
	}
	is_ufs = strcmp(vp->v_mount->mnt_vfc->vfc_name, "ufs") == 0;
	rbuf = malloc(siz, M_TEMP, M_WAITOK);
again:
	eofflag = 0;
	if (cookies) {
		free(cookies, M_TEMP);
		cookies = NULL;
	}

	iv.iov_base = rbuf;
	iv.iov_len = siz;
	io.uio_iov = &iv;
	io.uio_iovcnt = 1;
	io.uio_offset = (off_t)off;
	io.uio_resid = siz;
	io.uio_segflg = UIO_SYSSPACE;
	io.uio_rw = UIO_READ;
	io.uio_td = NULL;
	nd->nd_repstat = VOP_READDIR(vp, &io, nd->nd_cred, &eofflag, &ncookies,
	    &cookies);
	off = (u_int64_t)io.uio_offset;
	if (io.uio_resid)
		siz -= io.uio_resid;

	if (!cookies && !nd->nd_repstat)
		nd->nd_repstat = NFSERR_PERM;
	if (nd->nd_flag & ND_NFSV3) {
		getret = nfsvno_getattr(vp, &at, nd, p, 1, NULL);
		if (!nd->nd_repstat)
			nd->nd_repstat = getret;
	}

	/*
	 * Handles the failed cases. nd->nd_repstat == 0 past here.
	 */
	if (nd->nd_repstat) {
		vput(vp);
		free(rbuf, M_TEMP);
		if (cookies)
			free(cookies, M_TEMP);
		if (nd->nd_flag & ND_NFSV3)
			nfsrv_postopattr(nd, getret, &at);
		goto out;
	}
	/*
	 * If nothing read, return eof
	 * rpc reply
	 */
	if (siz == 0) {
		vput(vp);
		if (nd->nd_flag & ND_NFSV2) {
			NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
		} else {
			nfsrv_postopattr(nd, getret, &at);
			NFSM_BUILD(tl, u_int32_t *, 4 * NFSX_UNSIGNED);
			txdr_hyper(at.na_filerev, tl);
			tl += 2;
		}
		*tl++ = newnfs_false;
		*tl = newnfs_true;
		free(rbuf, M_TEMP);
		free(cookies, M_TEMP);
		goto out;
	}

	/*
	 * Check for degenerate cases of nothing useful read.
	 * If so go try again
	 */
	cpos = rbuf;
	cend = rbuf + siz;
	dp = (struct dirent *)cpos;
	cookiep = cookies;

	/*
	 * For some reason FreeBSD's ufs_readdir() chooses to back the
	 * directory offset up to a block boundary, so it is necessary to
	 * skip over the records that precede the requested offset. This
	 * requires the assumption that file offset cookies monotonically
	 * increase.
	 */
	while (cpos < cend && ncookies > 0 &&
	    (dp->d_fileno == 0 || dp->d_type == DT_WHT ||
	     (is_ufs == 1 && ((u_quad_t)(*cookiep)) <= toff))) {
		cpos += dp->d_reclen;
		dp = (struct dirent *)cpos;
		cookiep++;
		ncookies--;
	}
	if (cpos >= cend || ncookies == 0) {
		siz = fullsiz;
		toff = off;
		goto again;
	}
	vput(vp);

	/*
	 * dirlen is the size of the reply, including all XDR and must
	 * not exceed cnt. For NFSv2, RFC1094 didn't clearly indicate
	 * if the XDR should be included in "count", but to be safe, we do.
	 * (Include the two booleans at the end of the reply in dirlen now.)
	 */
	if (nd->nd_flag & ND_NFSV3) {
		nfsrv_postopattr(nd, getret, &at);
		NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
		txdr_hyper(at.na_filerev, tl);
		dirlen = NFSX_V3POSTOPATTR + NFSX_VERF + 2 * NFSX_UNSIGNED;
	} else {
		dirlen = 2 * NFSX_UNSIGNED;
	}

	/* Loop through the records and build reply */
	while (cpos < cend && ncookies > 0) {
		nlen = dp->d_namlen;
		if (dp->d_fileno != 0 && dp->d_type != DT_WHT &&
			nlen <= NFS_MAXNAMLEN) {
			if (nd->nd_flag & ND_NFSV3)
				dirlen += (6*NFSX_UNSIGNED + NFSM_RNDUP(nlen));
			else
				dirlen += (4*NFSX_UNSIGNED + NFSM_RNDUP(nlen));
			if (dirlen > cnt) {
				eofflag = 0;
				break;
			}

			/*
			 * Build the directory record xdr from
			 * the dirent entry.
			 */
			if (nd->nd_flag & ND_NFSV3) {
				NFSM_BUILD(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
				*tl++ = newnfs_true;
				*tl++ = 0;
			} else {
				NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
				*tl++ = newnfs_true;
			}
			*tl = txdr_unsigned(dp->d_fileno);
			(void) nfsm_strtom(nd, dp->d_name, nlen);
			if (nd->nd_flag & ND_NFSV3) {
				NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
				*tl++ = 0;
			} else
				NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(*cookiep);
		}
		cpos += dp->d_reclen;
		dp = (struct dirent *)cpos;
		cookiep++;
		ncookies--;
	}
	if (cpos < cend)
		eofflag = 0;
	NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
	*tl++ = newnfs_false;
	if (eofflag)
		*tl = newnfs_true;
	else
		*tl = newnfs_false;
	free(rbuf, M_TEMP);
	free(cookies, M_TEMP);

out:
	NFSEXITCODE2(0, nd);
	return (0);
nfsmout:
	vput(vp);
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Readdirplus for V3 and Readdir for V4.
 */
int
nfsrvd_readdirplus(struct nfsrv_descript *nd, int isdgram,
    struct vnode *vp, struct nfsexstuff *exp)
{
	struct dirent *dp;
	u_int32_t *tl;
	int dirlen;
	char *cpos, *cend, *rbuf;
	struct vnode *nvp;
	fhandle_t nfh;
	struct nfsvattr nva, at, *nvap = &nva;
	struct mbuf *mb0, *mb1;
	struct nfsreferral *refp;
	int nlen, r, error = 0, getret = 1, usevget = 1;
	int siz, cnt, fullsiz, eofflag, ncookies, entrycnt;
	caddr_t bpos0, bpos1;
	u_int64_t off, toff, verf;
	u_long *cookies = NULL, *cookiep;
	nfsattrbit_t attrbits, rderrbits, savbits;
	struct uio io;
	struct iovec iv;
	struct componentname cn;
	int at_root, is_ufs, is_zfs, needs_unbusy, supports_nfsv4acls;
	struct mount *mp, *new_mp;
	uint64_t mounted_on_fileno;
	struct thread *p = curthread;

	if (nd->nd_repstat) {
		nfsrv_postopattr(nd, getret, &at);
		goto out;
	}
	NFSM_DISSECT(tl, u_int32_t *, 6 * NFSX_UNSIGNED);
	off = fxdr_hyper(tl);
	toff = off;
	tl += 2;
	verf = fxdr_hyper(tl);
	tl += 2;
	siz = fxdr_unsigned(int, *tl++);
	cnt = fxdr_unsigned(int, *tl);

	/*
	 * Use the server's maximum data transfer size as the upper bound
	 * on reply datalen.
	 */
	if (cnt > NFS_SRVMAXDATA(nd) || cnt < 0)
		cnt = NFS_SRVMAXDATA(nd);

	/*
	 * siz is a "hint" of how much directory information (name, fileid,
	 * cookie) should be in the reply. At least one client "hints" 0,
	 * so I set it to cnt for that case. I also round it up to the
	 * next multiple of DIRBLKSIZ.
	 * Since the size of a Readdirplus directory entry reply will always
	 * be greater than a directory entry returned by VOP_READDIR(), it
	 * does not make sense to read more than NFS_SRVMAXDATA() via
	 * VOP_READDIR().
	 */
	if (siz <= 0)
		siz = cnt;
	else if (siz > NFS_SRVMAXDATA(nd))
		siz = NFS_SRVMAXDATA(nd);
	siz = ((siz + DIRBLKSIZ - 1) & ~(DIRBLKSIZ - 1));

	if (nd->nd_flag & ND_NFSV4) {
		error = nfsrv_getattrbits(nd, &attrbits, NULL, NULL);
		if (error)
			goto nfsmout;
		NFSSET_ATTRBIT(&savbits, &attrbits);
		NFSCLRNOTFILLABLE_ATTRBIT(&attrbits);
		NFSZERO_ATTRBIT(&rderrbits);
		NFSSETBIT_ATTRBIT(&rderrbits, NFSATTRBIT_RDATTRERROR);
	} else {
		NFSZERO_ATTRBIT(&attrbits);
	}
	fullsiz = siz;
	nd->nd_repstat = getret = nfsvno_getattr(vp, &at, nd, p, 1, NULL);
#if 0
	if (!nd->nd_repstat) {
	    if (off && verf != at.na_filerev) {
		/*
		 * va_filerev is not sufficient as a cookie verifier,
		 * since it is not supposed to change when entries are
		 * removed/added unless that offset cookies returned to
		 * the client are no longer valid.
		 */
		if (nd->nd_flag & ND_NFSV4) {
			nd->nd_repstat = NFSERR_NOTSAME;
		} else {
			nd->nd_repstat = NFSERR_BAD_COOKIE;
		}
	    }
	}
#endif
	if (!nd->nd_repstat && vp->v_type != VDIR)
		nd->nd_repstat = NFSERR_NOTDIR;
	if (!nd->nd_repstat && cnt == 0)
		nd->nd_repstat = NFSERR_TOOSMALL;
	if (!nd->nd_repstat)
		nd->nd_repstat = nfsvno_accchk(vp, VEXEC,
		    nd->nd_cred, exp, p, NFSACCCHK_NOOVERRIDE,
		    NFSACCCHK_VPISLOCKED, NULL);
	if (nd->nd_repstat) {
		vput(vp);
		if (nd->nd_flag & ND_NFSV3)
			nfsrv_postopattr(nd, getret, &at);
		goto out;
	}
	is_ufs = strcmp(vp->v_mount->mnt_vfc->vfc_name, "ufs") == 0;
	is_zfs = strcmp(vp->v_mount->mnt_vfc->vfc_name, "zfs") == 0;

	rbuf = malloc(siz, M_TEMP, M_WAITOK);
again:
	eofflag = 0;
	if (cookies) {
		free(cookies, M_TEMP);
		cookies = NULL;
	}

	iv.iov_base = rbuf;
	iv.iov_len = siz;
	io.uio_iov = &iv;
	io.uio_iovcnt = 1;
	io.uio_offset = (off_t)off;
	io.uio_resid = siz;
	io.uio_segflg = UIO_SYSSPACE;
	io.uio_rw = UIO_READ;
	io.uio_td = NULL;
	nd->nd_repstat = VOP_READDIR(vp, &io, nd->nd_cred, &eofflag, &ncookies,
	    &cookies);
	off = (u_int64_t)io.uio_offset;
	if (io.uio_resid)
		siz -= io.uio_resid;

	getret = nfsvno_getattr(vp, &at, nd, p, 1, NULL);

	if (!cookies && !nd->nd_repstat)
		nd->nd_repstat = NFSERR_PERM;
	if (!nd->nd_repstat)
		nd->nd_repstat = getret;
	if (nd->nd_repstat) {
		vput(vp);
		if (cookies)
			free(cookies, M_TEMP);
		free(rbuf, M_TEMP);
		if (nd->nd_flag & ND_NFSV3)
			nfsrv_postopattr(nd, getret, &at);
		goto out;
	}
	/*
	 * If nothing read, return eof
	 * rpc reply
	 */
	if (siz == 0) {
		vput(vp);
		if (nd->nd_flag & ND_NFSV3)
			nfsrv_postopattr(nd, getret, &at);
		NFSM_BUILD(tl, u_int32_t *, 4 * NFSX_UNSIGNED);
		txdr_hyper(at.na_filerev, tl);
		tl += 2;
		*tl++ = newnfs_false;
		*tl = newnfs_true;
		free(cookies, M_TEMP);
		free(rbuf, M_TEMP);
		goto out;
	}

	/*
	 * Check for degenerate cases of nothing useful read.
	 * If so go try again
	 */
	cpos = rbuf;
	cend = rbuf + siz;
	dp = (struct dirent *)cpos;
	cookiep = cookies;

	/*
	 * For some reason FreeBSD's ufs_readdir() chooses to back the
	 * directory offset up to a block boundary, so it is necessary to
	 * skip over the records that precede the requested offset. This
	 * requires the assumption that file offset cookies monotonically
	 * increase.
	 */
	while (cpos < cend && ncookies > 0 &&
	  (dp->d_fileno == 0 || dp->d_type == DT_WHT ||
	   (is_ufs == 1 && ((u_quad_t)(*cookiep)) <= toff) ||
	   ((nd->nd_flag & ND_NFSV4) &&
	    ((dp->d_namlen == 1 && dp->d_name[0] == '.') ||
	     (dp->d_namlen==2 && dp->d_name[0]=='.' && dp->d_name[1]=='.'))))) {
		cpos += dp->d_reclen;
		dp = (struct dirent *)cpos;
		cookiep++;
		ncookies--;
	}
	if (cpos >= cend || ncookies == 0) {
		siz = fullsiz;
		toff = off;
		goto again;
	}

	/*
	 * Busy the file system so that the mount point won't go away
	 * and, as such, VFS_VGET() can be used safely.
	 */
	mp = vp->v_mount;
	vfs_ref(mp);
	NFSVOPUNLOCK(vp, 0);
	nd->nd_repstat = vfs_busy(mp, 0);
	vfs_rel(mp);
	if (nd->nd_repstat != 0) {
		vrele(vp);
		free(cookies, M_TEMP);
		free(rbuf, M_TEMP);
		if (nd->nd_flag & ND_NFSV3)
			nfsrv_postopattr(nd, getret, &at);
		goto out;
	}

	/*
	 * Check to see if entries in this directory can be safely acquired
	 * via VFS_VGET() or if a switch to VOP_LOOKUP() is required.
	 * ZFS snapshot directories need VOP_LOOKUP(), so that any
	 * automount of the snapshot directory that is required will
	 * be done.
	 * This needs to be done here for NFSv4, since NFSv4 never does
	 * a VFS_VGET() for "." or "..".
	 */
	if (is_zfs == 1) {
		r = VFS_VGET(mp, at.na_fileid, LK_SHARED, &nvp);
		if (r == EOPNOTSUPP) {
			usevget = 0;
			cn.cn_nameiop = LOOKUP;
			cn.cn_lkflags = LK_SHARED | LK_RETRY;
			cn.cn_cred = nd->nd_cred;
			cn.cn_thread = p;
		} else if (r == 0)
			vput(nvp);
	}

	/*
	 * Save this position, in case there is an error before one entry
	 * is created.
	 */
	mb0 = nd->nd_mb;
	bpos0 = nd->nd_bpos;

	/*
	 * Fill in the first part of the reply.
	 * dirlen is the reply length in bytes and cannot exceed cnt.
	 * (Include the two booleans at the end of the reply in dirlen now,
	 *  so we recognize when we have exceeded cnt.)
	 */
	if (nd->nd_flag & ND_NFSV3) {
		dirlen = NFSX_V3POSTOPATTR + NFSX_VERF + 2 * NFSX_UNSIGNED;
		nfsrv_postopattr(nd, getret, &at);
	} else {
		dirlen = NFSX_VERF + 2 * NFSX_UNSIGNED;
	}
	NFSM_BUILD(tl, u_int32_t *, NFSX_VERF);
	txdr_hyper(at.na_filerev, tl);

	/*
	 * Save this position, in case there is an empty reply needed.
	 */
	mb1 = nd->nd_mb;
	bpos1 = nd->nd_bpos;

	/* Loop through the records and build reply */
	entrycnt = 0;
	while (cpos < cend && ncookies > 0 && dirlen < cnt) {
		nlen = dp->d_namlen;
		if (dp->d_fileno != 0 && dp->d_type != DT_WHT &&
		    nlen <= NFS_MAXNAMLEN &&
		    ((nd->nd_flag & ND_NFSV3) || nlen > 2 ||
		     (nlen==2 && (dp->d_name[0]!='.' || dp->d_name[1]!='.'))
		      || (nlen == 1 && dp->d_name[0] != '.'))) {
			/*
			 * Save the current position in the reply, in case
			 * this entry exceeds cnt.
			 */
			mb1 = nd->nd_mb;
			bpos1 = nd->nd_bpos;
	
			/*
			 * For readdir_and_lookup get the vnode using
			 * the file number.
			 */
			nvp = NULL;
			refp = NULL;
			r = 0;
			at_root = 0;
			needs_unbusy = 0;
			new_mp = mp;
			mounted_on_fileno = (uint64_t)dp->d_fileno;
			if ((nd->nd_flag & ND_NFSV3) ||
			    NFSNONZERO_ATTRBIT(&savbits)) {
				if (nd->nd_flag & ND_NFSV4)
					refp = nfsv4root_getreferral(NULL,
					    vp, dp->d_fileno);
				if (refp == NULL) {
					if (usevget)
						r = VFS_VGET(mp, dp->d_fileno,
						    LK_SHARED, &nvp);
					else
						r = EOPNOTSUPP;
					if (r == EOPNOTSUPP) {
						if (usevget) {
							usevget = 0;
							cn.cn_nameiop = LOOKUP;
							cn.cn_lkflags =
							    LK_SHARED |
							    LK_RETRY;
							cn.cn_cred =
							    nd->nd_cred;
							cn.cn_thread = p;
						}
						cn.cn_nameptr = dp->d_name;
						cn.cn_namelen = nlen;
						cn.cn_flags = ISLASTCN |
						    NOFOLLOW | LOCKLEAF;
						if (nlen == 2 &&
						    dp->d_name[0] == '.' &&
						    dp->d_name[1] == '.')
							cn.cn_flags |=
							    ISDOTDOT;
						if (NFSVOPLOCK(vp, LK_SHARED)
						    != 0) {
							nd->nd_repstat = EPERM;
							break;
						}
						if ((vp->v_vflag & VV_ROOT) != 0
						    && (cn.cn_flags & ISDOTDOT)
						    != 0) {
							vref(vp);
							nvp = vp;
							r = 0;
						} else {
							r = VOP_LOOKUP(vp, &nvp,
							    &cn);
							if (vp != nvp)
								NFSVOPUNLOCK(vp,
								    0);
						}
					}

					/*
					 * For NFSv4, check to see if nvp is
					 * a mount point and get the mount
					 * point vnode, as required.
					 */
					if (r == 0 &&
					    nfsrv_enable_crossmntpt != 0 &&
					    (nd->nd_flag & ND_NFSV4) != 0 &&
					    nvp->v_type == VDIR &&
					    nvp->v_mountedhere != NULL) {
						new_mp = nvp->v_mountedhere;
						r = vfs_busy(new_mp, 0);
						vput(nvp);
						nvp = NULL;
						if (r == 0) {
							r = VFS_ROOT(new_mp,
							    LK_SHARED, &nvp);
							needs_unbusy = 1;
							if (r == 0)
								at_root = 1;
						}
					}
				}

				/*
				 * If we failed to look up the entry, then it
				 * has become invalid, most likely removed.
				 */
				if (r != 0) {
					if (needs_unbusy)
						vfs_unbusy(new_mp);
					goto invalid;
				}
				KASSERT(refp != NULL || nvp != NULL,
				    ("%s: undetected lookup error", __func__));

				if (refp == NULL &&
				    ((nd->nd_flag & ND_NFSV3) ||
				     NFSNONZERO_ATTRBIT(&attrbits))) {
					r = nfsvno_getfh(nvp, &nfh, p);
					if (!r)
					    r = nfsvno_getattr(nvp, nvap, nd, p,
						1, &attrbits);
					if (r == 0 && is_zfs == 1 &&
					    nfsrv_enable_crossmntpt != 0 &&
					    (nd->nd_flag & ND_NFSV4) != 0 &&
					    nvp->v_type == VDIR &&
					    vp->v_mount != nvp->v_mount) {
					    /*
					     * For a ZFS snapshot, there is a
					     * pseudo mount that does not set
					     * v_mountedhere, so it needs to
					     * be detected via a different
					     * mount structure.
					     */
					    at_root = 1;
					    if (new_mp == mp)
						new_mp = nvp->v_mount;
					}
				}

				/*
				 * If we failed to get attributes of the entry,
				 * then just skip it for NFSv3 (the traditional
				 * behavior in the old NFS server).
				 * For NFSv4 the behavior is controlled by
				 * RDATTRERROR: we either ignore the error or
				 * fail the request.
				 * Note that RDATTRERROR is never set for NFSv3.
				 */
				if (r != 0) {
					if (!NFSISSET_ATTRBIT(&attrbits,
					    NFSATTRBIT_RDATTRERROR)) {
						vput(nvp);
						if (needs_unbusy != 0)
							vfs_unbusy(new_mp);
						if ((nd->nd_flag & ND_NFSV3))
							goto invalid;
						nd->nd_repstat = r;
						break;
					}
				}
			}

			/*
			 * Build the directory record xdr
			 */
			if (nd->nd_flag & ND_NFSV3) {
				NFSM_BUILD(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
				*tl++ = newnfs_true;
				*tl++ = 0;
				*tl = txdr_unsigned(dp->d_fileno);
				dirlen += nfsm_strtom(nd, dp->d_name, nlen);
				NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
				*tl++ = 0;
				*tl = txdr_unsigned(*cookiep);
				nfsrv_postopattr(nd, 0, nvap);
				dirlen += nfsm_fhtom(nd,(u_int8_t *)&nfh,0,1);
				dirlen += (5*NFSX_UNSIGNED+NFSX_V3POSTOPATTR);
				if (nvp != NULL)
					vput(nvp);
			} else {
				NFSM_BUILD(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
				*tl++ = newnfs_true;
				*tl++ = 0;
				*tl = txdr_unsigned(*cookiep);
				dirlen += nfsm_strtom(nd, dp->d_name, nlen);
				if (nvp != NULL) {
					supports_nfsv4acls =
					    nfs_supportsnfsv4acls(nvp);
					NFSVOPUNLOCK(nvp, 0);
				} else
					supports_nfsv4acls = 0;
				if (refp != NULL) {
					dirlen += nfsrv_putreferralattr(nd,
					    &savbits, refp, 0,
					    &nd->nd_repstat);
					if (nd->nd_repstat) {
						if (nvp != NULL)
							vrele(nvp);
						if (needs_unbusy != 0)
							vfs_unbusy(new_mp);
						break;
					}
				} else if (r) {
					dirlen += nfsvno_fillattr(nd, new_mp,
					    nvp, nvap, &nfh, r, &rderrbits,
					    nd->nd_cred, p, isdgram, 0,
					    supports_nfsv4acls, at_root,
					    mounted_on_fileno);
				} else {
					dirlen += nfsvno_fillattr(nd, new_mp,
					    nvp, nvap, &nfh, r, &attrbits,
					    nd->nd_cred, p, isdgram, 0,
					    supports_nfsv4acls, at_root,
					    mounted_on_fileno);
				}
				if (nvp != NULL)
					vrele(nvp);
				dirlen += (3 * NFSX_UNSIGNED);
			}
			if (needs_unbusy != 0)
				vfs_unbusy(new_mp);
			if (dirlen <= cnt)
				entrycnt++;
		}
invalid:
		cpos += dp->d_reclen;
		dp = (struct dirent *)cpos;
		cookiep++;
		ncookies--;
	}
	vrele(vp);
	vfs_unbusy(mp);

	/*
	 * If dirlen > cnt, we must strip off the last entry. If that
	 * results in an empty reply, report NFSERR_TOOSMALL.
	 */
	if (dirlen > cnt || nd->nd_repstat) {
		if (!nd->nd_repstat && entrycnt == 0)
			nd->nd_repstat = NFSERR_TOOSMALL;
		if (nd->nd_repstat) {
			newnfs_trimtrailing(nd, mb0, bpos0);
			if (nd->nd_flag & ND_NFSV3)
				nfsrv_postopattr(nd, getret, &at);
		} else
			newnfs_trimtrailing(nd, mb1, bpos1);
		eofflag = 0;
	} else if (cpos < cend)
		eofflag = 0;
	if (!nd->nd_repstat) {
		NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
		*tl++ = newnfs_false;
		if (eofflag)
			*tl = newnfs_true;
		else
			*tl = newnfs_false;
	}
	free(cookies, M_TEMP);
	free(rbuf, M_TEMP);

out:
	NFSEXITCODE2(0, nd);
	return (0);
nfsmout:
	vput(vp);
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Get the settable attributes out of the mbuf list.
 * (Return 0 or EBADRPC)
 */
int
nfsrv_sattr(struct nfsrv_descript *nd, vnode_t vp, struct nfsvattr *nvap,
    nfsattrbit_t *attrbitp, NFSACL_T *aclp, struct thread *p)
{
	u_int32_t *tl;
	struct nfsv2_sattr *sp;
	int error = 0, toclient = 0;

	switch (nd->nd_flag & (ND_NFSV2 | ND_NFSV3 | ND_NFSV4)) {
	case ND_NFSV2:
		NFSM_DISSECT(sp, struct nfsv2_sattr *, NFSX_V2SATTR);
		/*
		 * Some old clients didn't fill in the high order 16bits.
		 * --> check the low order 2 bytes for 0xffff
		 */
		if ((fxdr_unsigned(int, sp->sa_mode) & 0xffff) != 0xffff)
			nvap->na_mode = nfstov_mode(sp->sa_mode);
		if (sp->sa_uid != newnfs_xdrneg1)
			nvap->na_uid = fxdr_unsigned(uid_t, sp->sa_uid);
		if (sp->sa_gid != newnfs_xdrneg1)
			nvap->na_gid = fxdr_unsigned(gid_t, sp->sa_gid);
		if (sp->sa_size != newnfs_xdrneg1)
			nvap->na_size = fxdr_unsigned(u_quad_t, sp->sa_size);
		if (sp->sa_atime.nfsv2_sec != newnfs_xdrneg1) {
#ifdef notyet
			fxdr_nfsv2time(&sp->sa_atime, &nvap->na_atime);
#else
			nvap->na_atime.tv_sec =
				fxdr_unsigned(u_int32_t,sp->sa_atime.nfsv2_sec);
			nvap->na_atime.tv_nsec = 0;
#endif
		}
		if (sp->sa_mtime.nfsv2_sec != newnfs_xdrneg1)
			fxdr_nfsv2time(&sp->sa_mtime, &nvap->na_mtime);
		break;
	case ND_NFSV3:
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		if (*tl == newnfs_true) {
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			nvap->na_mode = nfstov_mode(*tl);
		}
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		if (*tl == newnfs_true) {
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			nvap->na_uid = fxdr_unsigned(uid_t, *tl);
		}
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		if (*tl == newnfs_true) {
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			nvap->na_gid = fxdr_unsigned(gid_t, *tl);
		}
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		if (*tl == newnfs_true) {
			NFSM_DISSECT(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			nvap->na_size = fxdr_hyper(tl);
		}
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		switch (fxdr_unsigned(int, *tl)) {
		case NFSV3SATTRTIME_TOCLIENT:
			NFSM_DISSECT(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			fxdr_nfsv3time(tl, &nvap->na_atime);
			toclient = 1;
			break;
		case NFSV3SATTRTIME_TOSERVER:
			vfs_timestamp(&nvap->na_atime);
			nvap->na_vaflags |= VA_UTIMES_NULL;
			break;
		}
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		switch (fxdr_unsigned(int, *tl)) {
		case NFSV3SATTRTIME_TOCLIENT:
			NFSM_DISSECT(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			fxdr_nfsv3time(tl, &nvap->na_mtime);
			nvap->na_vaflags &= ~VA_UTIMES_NULL;
			break;
		case NFSV3SATTRTIME_TOSERVER:
			vfs_timestamp(&nvap->na_mtime);
			if (!toclient)
				nvap->na_vaflags |= VA_UTIMES_NULL;
			break;
		}
		break;
	case ND_NFSV4:
		error = nfsv4_sattr(nd, vp, nvap, attrbitp, aclp, p);
	}
nfsmout:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Handle the setable attributes for V4.
 * Returns NFSERR_BADXDR if it can't be parsed, 0 otherwise.
 */
int
nfsv4_sattr(struct nfsrv_descript *nd, vnode_t vp, struct nfsvattr *nvap,
    nfsattrbit_t *attrbitp, NFSACL_T *aclp, struct thread *p)
{
	u_int32_t *tl;
	int attrsum = 0;
	int i, j;
	int error, attrsize, bitpos, aclsize, aceerr, retnotsup = 0;
	int toclient = 0;
	u_char *cp, namestr[NFSV4_SMALLSTR + 1];
	uid_t uid;
	gid_t gid;

	error = nfsrv_getattrbits(nd, attrbitp, NULL, &retnotsup);
	if (error)
		goto nfsmout;
	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	attrsize = fxdr_unsigned(int, *tl);

	/*
	 * Loop around getting the setable attributes. If an unsupported
	 * one is found, set nd_repstat == NFSERR_ATTRNOTSUPP and return.
	 */
	if (retnotsup) {
		nd->nd_repstat = NFSERR_ATTRNOTSUPP;
		bitpos = NFSATTRBIT_MAX;
	} else {
		bitpos = 0;
	}
	for (; bitpos < NFSATTRBIT_MAX; bitpos++) {
	    if (attrsum > attrsize) {
		error = NFSERR_BADXDR;
		goto nfsmout;
	    }
	    if (NFSISSET_ATTRBIT(attrbitp, bitpos))
		switch (bitpos) {
		case NFSATTRBIT_SIZE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
                     if (vp != NULL && vp->v_type != VREG) {
                            error = (vp->v_type == VDIR) ? NFSERR_ISDIR :
                                NFSERR_INVAL;
                            goto nfsmout;
			}
			nvap->na_size = fxdr_hyper(tl);
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_ACL:
			error = nfsrv_dissectacl(nd, aclp, &aceerr, &aclsize,
			    p);
			if (error)
				goto nfsmout;
			if (aceerr && !nd->nd_repstat)
				nd->nd_repstat = aceerr;
			attrsum += aclsize;
			break;
		case NFSATTRBIT_ARCHIVE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (!nd->nd_repstat)
				nd->nd_repstat = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_HIDDEN:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (!nd->nd_repstat)
				nd->nd_repstat = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_MIMETYPE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			i = fxdr_unsigned(int, *tl);
			error = nfsm_advance(nd, NFSM_RNDUP(i), -1);
			if (error)
				goto nfsmout;
			if (!nd->nd_repstat)
				nd->nd_repstat = NFSERR_ATTRNOTSUPP;
			attrsum += (NFSX_UNSIGNED + NFSM_RNDUP(i));
			break;
		case NFSATTRBIT_MODE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			nvap->na_mode = nfstov_mode(*tl);
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_OWNER:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			j = fxdr_unsigned(int, *tl);
			if (j < 0) {
				error = NFSERR_BADXDR;
				goto nfsmout;
			}
			if (j > NFSV4_SMALLSTR)
				cp = malloc(j + 1, M_NFSSTRING, M_WAITOK);
			else
				cp = namestr;
			error = nfsrv_mtostr(nd, cp, j);
			if (error) {
				if (j > NFSV4_SMALLSTR)
					free(cp, M_NFSSTRING);
				goto nfsmout;
			}
			if (!nd->nd_repstat) {
				nd->nd_repstat = nfsv4_strtouid(nd, cp, j,
				    &uid);
				if (!nd->nd_repstat)
					nvap->na_uid = uid;
			}
			if (j > NFSV4_SMALLSTR)
				free(cp, M_NFSSTRING);
			attrsum += (NFSX_UNSIGNED + NFSM_RNDUP(j));
			break;
		case NFSATTRBIT_OWNERGROUP:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			j = fxdr_unsigned(int, *tl);
			if (j < 0) {
				error = NFSERR_BADXDR;
				goto nfsmout;
			}
			if (j > NFSV4_SMALLSTR)
				cp = malloc(j + 1, M_NFSSTRING, M_WAITOK);
			else
				cp = namestr;
			error = nfsrv_mtostr(nd, cp, j);
			if (error) {
				if (j > NFSV4_SMALLSTR)
					free(cp, M_NFSSTRING);
				goto nfsmout;
			}
			if (!nd->nd_repstat) {
				nd->nd_repstat = nfsv4_strtogid(nd, cp, j,
				    &gid);
				if (!nd->nd_repstat)
					nvap->na_gid = gid;
			}
			if (j > NFSV4_SMALLSTR)
				free(cp, M_NFSSTRING);
			attrsum += (NFSX_UNSIGNED + NFSM_RNDUP(j));
			break;
		case NFSATTRBIT_SYSTEM:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (!nd->nd_repstat)
				nd->nd_repstat = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_TIMEACCESSSET:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			attrsum += NFSX_UNSIGNED;
			if (fxdr_unsigned(int, *tl)==NFSV4SATTRTIME_TOCLIENT) {
			    NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			    fxdr_nfsv4time(tl, &nvap->na_atime);
			    toclient = 1;
			    attrsum += NFSX_V4TIME;
			} else {
			    vfs_timestamp(&nvap->na_atime);
			    nvap->na_vaflags |= VA_UTIMES_NULL;
			}
			break;
		case NFSATTRBIT_TIMEBACKUP:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			if (!nd->nd_repstat)
				nd->nd_repstat = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMECREATE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			if (!nd->nd_repstat)
				nd->nd_repstat = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEMODIFYSET:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			attrsum += NFSX_UNSIGNED;
			if (fxdr_unsigned(int, *tl)==NFSV4SATTRTIME_TOCLIENT) {
			    NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			    fxdr_nfsv4time(tl, &nvap->na_mtime);
			    nvap->na_vaflags &= ~VA_UTIMES_NULL;
			    attrsum += NFSX_V4TIME;
			} else {
			    vfs_timestamp(&nvap->na_mtime);
			    if (!toclient)
				nvap->na_vaflags |= VA_UTIMES_NULL;
			}
			break;
		default:
			nd->nd_repstat = NFSERR_ATTRNOTSUPP;
			/*
			 * set bitpos so we drop out of the loop.
			 */
			bitpos = NFSATTRBIT_MAX;
			break;
		}
	}

	/*
	 * some clients pad the attrlist, so we need to skip over the
	 * padding.
	 */
	if (attrsum > attrsize) {
		error = NFSERR_BADXDR;
	} else {
		attrsize = NFSM_RNDUP(attrsize);
		if (attrsum < attrsize)
			error = nfsm_advance(nd, attrsize - attrsum, -1);
	}
nfsmout:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Check/setup export credentials.
 */
int
nfsd_excred(struct nfsrv_descript *nd, struct nfsexstuff *exp,
    struct ucred *credanon)
{
	int error = 0;

	/*
	 * Check/setup credentials.
	 */
	if (nd->nd_flag & ND_GSS)
		exp->nes_exflag &= ~MNT_EXPORTANON;

	/*
	 * Check to see if the operation is allowed for this security flavor.
	 * RFC2623 suggests that the NFSv3 Fsinfo RPC be allowed to
	 * AUTH_NONE or AUTH_SYS for file systems requiring RPCSEC_GSS.
	 * Also, allow Secinfo, so that it can acquire the correct flavor(s).
	 */
	if (nfsvno_testexp(nd, exp) &&
	    nd->nd_procnum != NFSV4OP_SECINFO &&
	    nd->nd_procnum != NFSPROC_FSINFO) {
		if (nd->nd_flag & ND_NFSV4)
			error = NFSERR_WRONGSEC;
		else
			error = (NFSERR_AUTHERR | AUTH_TOOWEAK);
		goto out;
	}

	/*
	 * Check to see if the file system is exported V4 only.
	 */
	if (NFSVNO_EXV4ONLY(exp) && !(nd->nd_flag & ND_NFSV4)) {
		error = NFSERR_PROGNOTV4;
		goto out;
	}

	/*
	 * Now, map the user credentials.
	 * (Note that ND_AUTHNONE will only be set for an NFSv3
	 *  Fsinfo RPC. If set for anything else, this code might need
	 *  to change.)
	 */
	if (NFSVNO_EXPORTED(exp)) {
		if (((nd->nd_flag & ND_GSS) == 0 && nd->nd_cred->cr_uid == 0) ||
		     NFSVNO_EXPORTANON(exp) ||
		     (nd->nd_flag & ND_AUTHNONE) != 0) {
			nd->nd_cred->cr_uid = credanon->cr_uid;
			nd->nd_cred->cr_gid = credanon->cr_gid;
			crsetgroups(nd->nd_cred, credanon->cr_ngroups,
			    credanon->cr_groups);
		} else if ((nd->nd_flag & ND_GSS) == 0) {
			/*
			 * If using AUTH_SYS, call nfsrv_getgrpscred() to see
			 * if there is a replacement credential with a group
			 * list set up by "nfsuserd -manage-gids".
			 * If there is no replacement, nfsrv_getgrpscred()
			 * simply returns its argument.
			 */
			nd->nd_cred = nfsrv_getgrpscred(nd->nd_cred);
		}
	}

out:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Check exports.
 */
int
nfsvno_checkexp(struct mount *mp, struct sockaddr *nam, struct nfsexstuff *exp,
    struct ucred **credp)
{
	int i, error, *secflavors;

	error = VFS_CHECKEXP(mp, nam, &exp->nes_exflag, credp,
	    &exp->nes_numsecflavor, &secflavors);
	if (error) {
		if (nfs_rootfhset) {
			exp->nes_exflag = 0;
			exp->nes_numsecflavor = 0;
			error = 0;
		}
	} else {
		/* Copy the security flavors. */
		for (i = 0; i < exp->nes_numsecflavor; i++)
			exp->nes_secflavors[i] = secflavors[i];
	}
	NFSEXITCODE(error);
	return (error);
}

/*
 * Get a vnode for a file handle and export stuff.
 */
int
nfsvno_fhtovp(struct mount *mp, fhandle_t *fhp, struct sockaddr *nam,
    int lktype, struct vnode **vpp, struct nfsexstuff *exp,
    struct ucred **credp)
{
	int i, error, *secflavors;

	*credp = NULL;
	exp->nes_numsecflavor = 0;
	error = VFS_FHTOVP(mp, &fhp->fh_fid, lktype, vpp);
	if (error != 0)
		/* Make sure the server replies ESTALE to the client. */
		error = ESTALE;
	if (nam && !error) {
		error = VFS_CHECKEXP(mp, nam, &exp->nes_exflag, credp,
		    &exp->nes_numsecflavor, &secflavors);
		if (error) {
			if (nfs_rootfhset) {
				exp->nes_exflag = 0;
				exp->nes_numsecflavor = 0;
				error = 0;
			} else {
				vput(*vpp);
			}
		} else {
			/* Copy the security flavors. */
			for (i = 0; i < exp->nes_numsecflavor; i++)
				exp->nes_secflavors[i] = secflavors[i];
		}
	}
	NFSEXITCODE(error);
	return (error);
}

/*
 * nfsd_fhtovp() - convert a fh to a vnode ptr
 * 	- look up fsid in mount list (if not found ret error)
 *	- get vp and export rights by calling nfsvno_fhtovp()
 *	- if cred->cr_uid == 0 or MNT_EXPORTANON set it to credanon
 *	  for AUTH_SYS
 *	- if mpp != NULL, return the mount point so that it can
 *	  be used for vn_finished_write() by the caller
 */
void
nfsd_fhtovp(struct nfsrv_descript *nd, struct nfsrvfh *nfp, int lktype,
    struct vnode **vpp, struct nfsexstuff *exp,
    struct mount **mpp, int startwrite)
{
	struct mount *mp;
	struct ucred *credanon;
	fhandle_t *fhp;

	fhp = (fhandle_t *)nfp->nfsrvfh_data;
	/*
	 * Check for the special case of the nfsv4root_fh.
	 */
	mp = vfs_busyfs(&fhp->fh_fsid);
	if (mpp != NULL)
		*mpp = mp;
	if (mp == NULL) {
		*vpp = NULL;
		nd->nd_repstat = ESTALE;
		goto out;
	}

	if (startwrite) {
		vn_start_write(NULL, mpp, V_WAIT);
		if (lktype == LK_SHARED && !(MNT_SHARED_WRITES(mp)))
			lktype = LK_EXCLUSIVE;
	}
	nd->nd_repstat = nfsvno_fhtovp(mp, fhp, nd->nd_nam, lktype, vpp, exp,
	    &credanon);
	vfs_unbusy(mp);

	/*
	 * For NFSv4 without a pseudo root fs, unexported file handles
	 * can be returned, so that Lookup works everywhere.
	 */
	if (!nd->nd_repstat && exp->nes_exflag == 0 &&
	    !(nd->nd_flag & ND_NFSV4)) {
		vput(*vpp);
		nd->nd_repstat = EACCES;
	}

	/*
	 * Personally, I've never seen any point in requiring a
	 * reserved port#, since only in the rare case where the
	 * clients are all boxes with secure system privileges,
	 * does it provide any enhanced security, but... some people
	 * believe it to be useful and keep putting this code back in.
	 * (There is also some "security checker" out there that
	 *  complains if the nfs server doesn't enforce this.)
	 * However, note the following:
	 * RFC3530 (NFSv4) specifies that a reserved port# not be
	 *	required.
	 * RFC2623 recommends that, if a reserved port# is checked for,
	 *	that there be a way to turn that off--> ifdef'd.
	 */
#ifdef NFS_REQRSVPORT
	if (!nd->nd_repstat) {
		struct sockaddr_in *saddr;
		struct sockaddr_in6 *saddr6;

		saddr = NFSSOCKADDR(nd->nd_nam, struct sockaddr_in *);
		saddr6 = NFSSOCKADDR(nd->nd_nam, struct sockaddr_in6 *);
		if (!(nd->nd_flag & ND_NFSV4) &&
		    ((saddr->sin_family == AF_INET &&
		      ntohs(saddr->sin_port) >= IPPORT_RESERVED) ||
		     (saddr6->sin6_family == AF_INET6 &&
		      ntohs(saddr6->sin6_port) >= IPPORT_RESERVED))) {
			vput(*vpp);
			nd->nd_repstat = (NFSERR_AUTHERR | AUTH_TOOWEAK);
		}
	}
#endif	/* NFS_REQRSVPORT */

	/*
	 * Check/setup credentials.
	 */
	if (!nd->nd_repstat) {
		nd->nd_saveduid = nd->nd_cred->cr_uid;
		nd->nd_repstat = nfsd_excred(nd, exp, credanon);
		if (nd->nd_repstat)
			vput(*vpp);
	}
	if (credanon != NULL)
		crfree(credanon);
	if (nd->nd_repstat) {
		if (startwrite)
			vn_finished_write(mp);
		*vpp = NULL;
		if (mpp != NULL)
			*mpp = NULL;
	}

out:
	NFSEXITCODE2(0, nd);
}

/*
 * glue for fp.
 */
static int
fp_getfvp(struct thread *p, int fd, struct file **fpp, struct vnode **vpp)
{
	struct filedesc *fdp;
	struct file *fp;
	int error = 0;

	fdp = p->td_proc->p_fd;
	if (fd < 0 || fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[fd].fde_file) == NULL) {
		error = EBADF;
		goto out;
	}
	*fpp = fp;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Called from nfssvc() to update the exports list. Just call
 * vfs_export(). This has to be done, since the v4 root fake fs isn't
 * in the mount list.
 */
int
nfsrv_v4rootexport(void *argp, struct ucred *cred, struct thread *p)
{
	struct nfsex_args *nfsexargp = (struct nfsex_args *)argp;
	int error = 0;
	struct nameidata nd;
	fhandle_t fh;

	error = vfs_export(&nfsv4root_mnt, &nfsexargp->export);
	if ((nfsexargp->export.ex_flags & MNT_DELEXPORT) != 0)
		nfs_rootfhset = 0;
	else if (error == 0) {
		if (nfsexargp->fspec == NULL) {
			error = EPERM;
			goto out;
		}
		/*
		 * If fspec != NULL, this is the v4root path.
		 */
		NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE,
		    nfsexargp->fspec, p);
		if ((error = namei(&nd)) != 0)
			goto out;
		error = nfsvno_getfh(nd.ni_vp, &fh, p);
		vrele(nd.ni_vp);
		if (!error) {
			nfs_rootfh.nfsrvfh_len = NFSX_MYFH;
			NFSBCOPY((caddr_t)&fh,
			    nfs_rootfh.nfsrvfh_data,
			    sizeof (fhandle_t));
			nfs_rootfhset = 1;
		}
	}

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * This function needs to test to see if the system is near its limit
 * for memory allocation via malloc() or mget() and return True iff
 * either of these resources are near their limit.
 * XXX (For now, this is just a stub.)
 */
int nfsrv_testmalloclimit = 0;
int
nfsrv_mallocmget_limit(void)
{
	static int printmesg = 0;
	static int testval = 1;

	if (nfsrv_testmalloclimit && (testval++ % 1000) == 0) {
		if ((printmesg++ % 100) == 0)
			printf("nfsd: malloc/mget near limit\n");
		return (1);
	}
	return (0);
}

/*
 * BSD specific initialization of a mount point.
 */
void
nfsd_mntinit(void)
{
	static int inited = 0;

	if (inited)
		return;
	inited = 1;
	nfsv4root_mnt.mnt_flag = (MNT_RDONLY | MNT_EXPORTED);
	TAILQ_INIT(&nfsv4root_mnt.mnt_nvnodelist);
	TAILQ_INIT(&nfsv4root_mnt.mnt_activevnodelist);
	nfsv4root_mnt.mnt_export = NULL;
	TAILQ_INIT(&nfsv4root_opt);
	TAILQ_INIT(&nfsv4root_newopt);
	nfsv4root_mnt.mnt_opt = &nfsv4root_opt;
	nfsv4root_mnt.mnt_optnew = &nfsv4root_newopt;
	nfsv4root_mnt.mnt_nvnodelistsize = 0;
	nfsv4root_mnt.mnt_activevnodelistsize = 0;
}

/*
 * Get a vnode for a file handle, without checking exports, etc.
 */
struct vnode *
nfsvno_getvp(fhandle_t *fhp)
{
	struct mount *mp;
	struct vnode *vp;
	int error;

	mp = vfs_busyfs(&fhp->fh_fsid);
	if (mp == NULL)
		return (NULL);
	error = VFS_FHTOVP(mp, &fhp->fh_fid, LK_EXCLUSIVE, &vp);
	vfs_unbusy(mp);
	if (error)
		return (NULL);
	return (vp);
}

/*
 * Do a local VOP_ADVLOCK().
 */
int
nfsvno_advlock(struct vnode *vp, int ftype, u_int64_t first,
    u_int64_t end, struct thread *td)
{
	int error = 0;
	struct flock fl;
	u_int64_t tlen;

	if (nfsrv_dolocallocks == 0)
		goto out;
	ASSERT_VOP_UNLOCKED(vp, "nfsvno_advlock: vp locked");

	fl.l_whence = SEEK_SET;
	fl.l_type = ftype;
	fl.l_start = (off_t)first;
	if (end == NFS64BITSSET) {
		fl.l_len = 0;
	} else {
		tlen = end - first;
		fl.l_len = (off_t)tlen;
	}
	/*
	 * For FreeBSD8, the l_pid and l_sysid must be set to the same
	 * values for all calls, so that all locks will be held by the
	 * nfsd server. (The nfsd server handles conflicts between the
	 * various clients.)
	 * Since an NFSv4 lockowner is a ClientID plus an array of up to 1024
	 * bytes, so it can't be put in l_sysid.
	 */
	if (nfsv4_sysid == 0)
		nfsv4_sysid = nlm_acquire_next_sysid();
	fl.l_pid = (pid_t)0;
	fl.l_sysid = (int)nfsv4_sysid;

	if (ftype == F_UNLCK)
		error = VOP_ADVLOCK(vp, (caddr_t)td->td_proc, F_UNLCK, &fl,
		    (F_POSIX | F_REMOTE));
	else
		error = VOP_ADVLOCK(vp, (caddr_t)td->td_proc, F_SETLK, &fl,
		    (F_POSIX | F_REMOTE));

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Check the nfsv4 root exports.
 */
int
nfsvno_v4rootexport(struct nfsrv_descript *nd)
{
	struct ucred *credanon;
	int exflags, error = 0, numsecflavor, *secflavors, i;

	error = vfs_stdcheckexp(&nfsv4root_mnt, nd->nd_nam, &exflags,
	    &credanon, &numsecflavor, &secflavors);
	if (error) {
		error = NFSERR_PROGUNAVAIL;
		goto out;
	}
	if (credanon != NULL)
		crfree(credanon);
	for (i = 0; i < numsecflavor; i++) {
		if (secflavors[i] == AUTH_SYS)
			nd->nd_flag |= ND_EXAUTHSYS;
		else if (secflavors[i] == RPCSEC_GSS_KRB5)
			nd->nd_flag |= ND_EXGSS;
		else if (secflavors[i] == RPCSEC_GSS_KRB5I)
			nd->nd_flag |= ND_EXGSSINTEGRITY;
		else if (secflavors[i] == RPCSEC_GSS_KRB5P)
			nd->nd_flag |= ND_EXGSSPRIVACY;
	}

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Nfs server pseudo system call for the nfsd's
 */
/*
 * MPSAFE
 */
static int
nfssvc_nfsd(struct thread *td, struct nfssvc_args *uap)
{
	struct file *fp;
	struct nfsd_addsock_args sockarg;
	struct nfsd_nfsd_args nfsdarg;
	struct nfsd_nfsd_oargs onfsdarg;
	struct nfsd_pnfsd_args pnfsdarg;
	struct vnode *vp, *nvp, *curdvp;
	struct pnfsdsfile *pf;
	struct nfsdevice *ds, *fds;
	cap_rights_t rights;
	int buflen, error, ret;
	char *buf, *cp, *cp2, *cp3;
	char fname[PNFS_FILENAME_LEN + 1];

	if (uap->flag & NFSSVC_NFSDADDSOCK) {
		error = copyin(uap->argp, (caddr_t)&sockarg, sizeof (sockarg));
		if (error)
			goto out;
		/*
		 * Since we don't know what rights might be required,
		 * pretend that we need them all. It is better to be too
		 * careful than too reckless.
		 */
		error = fget(td, sockarg.sock,
		    cap_rights_init(&rights, CAP_SOCK_SERVER), &fp);
		if (error != 0)
			goto out;
		if (fp->f_type != DTYPE_SOCKET) {
			fdrop(fp, td);
			error = EPERM;
			goto out;
		}
		error = nfsrvd_addsock(fp);
		fdrop(fp, td);
	} else if (uap->flag & NFSSVC_NFSDNFSD) {
		if (uap->argp == NULL) {
			error = EINVAL;
			goto out;
		}
		if ((uap->flag & NFSSVC_NEWSTRUCT) == 0) {
			error = copyin(uap->argp, &onfsdarg, sizeof(onfsdarg));
			if (error == 0) {
				nfsdarg.principal = onfsdarg.principal;
				nfsdarg.minthreads = onfsdarg.minthreads;
				nfsdarg.maxthreads = onfsdarg.maxthreads;
				nfsdarg.version = 1;
				nfsdarg.addr = NULL;
				nfsdarg.addrlen = 0;
				nfsdarg.dnshost = NULL;
				nfsdarg.dnshostlen = 0;
				nfsdarg.dspath = NULL;
				nfsdarg.dspathlen = 0;
				nfsdarg.mdspath = NULL;
				nfsdarg.mdspathlen = 0;
				nfsdarg.mirrorcnt = 1;
			}
		} else
			error = copyin(uap->argp, &nfsdarg, sizeof(nfsdarg));
		if (error)
			goto out;
		if (nfsdarg.addrlen > 0 && nfsdarg.addrlen < 10000 &&
		    nfsdarg.dnshostlen > 0 && nfsdarg.dnshostlen < 10000 &&
		    nfsdarg.dspathlen > 0 && nfsdarg.dspathlen < 10000 &&
		    nfsdarg.mdspathlen > 0 && nfsdarg.mdspathlen < 10000 &&
		    nfsdarg.mirrorcnt >= 1 &&
		    nfsdarg.mirrorcnt <= NFSDEV_MAXMIRRORS &&
		    nfsdarg.addr != NULL && nfsdarg.dnshost != NULL &&
		    nfsdarg.dspath != NULL && nfsdarg.mdspath != NULL) {
			NFSD_DEBUG(1, "addrlen=%d dspathlen=%d dnslen=%d"
			    " mdspathlen=%d mirrorcnt=%d\n", nfsdarg.addrlen,
			    nfsdarg.dspathlen, nfsdarg.dnshostlen,
			    nfsdarg.mdspathlen, nfsdarg.mirrorcnt);
			cp = malloc(nfsdarg.addrlen + 1, M_TEMP, M_WAITOK);
			error = copyin(nfsdarg.addr, cp, nfsdarg.addrlen);
			if (error != 0) {
				free(cp, M_TEMP);
				goto out;
			}
			cp[nfsdarg.addrlen] = '\0';	/* Ensure nul term. */
			nfsdarg.addr = cp;
			cp = malloc(nfsdarg.dnshostlen + 1, M_TEMP, M_WAITOK);
			error = copyin(nfsdarg.dnshost, cp, nfsdarg.dnshostlen);
			if (error != 0) {
				free(nfsdarg.addr, M_TEMP);
				free(cp, M_TEMP);
				goto out;
			}
			cp[nfsdarg.dnshostlen] = '\0';	/* Ensure nul term. */
			nfsdarg.dnshost = cp;
			cp = malloc(nfsdarg.dspathlen + 1, M_TEMP, M_WAITOK);
			error = copyin(nfsdarg.dspath, cp, nfsdarg.dspathlen);
			if (error != 0) {
				free(nfsdarg.addr, M_TEMP);
				free(nfsdarg.dnshost, M_TEMP);
				free(cp, M_TEMP);
				goto out;
			}
			cp[nfsdarg.dspathlen] = '\0';	/* Ensure nul term. */
			nfsdarg.dspath = cp;
			cp = malloc(nfsdarg.mdspathlen + 1, M_TEMP, M_WAITOK);
			error = copyin(nfsdarg.mdspath, cp, nfsdarg.mdspathlen);
			if (error != 0) {
				free(nfsdarg.addr, M_TEMP);
				free(nfsdarg.dnshost, M_TEMP);
				free(nfsdarg.dspath, M_TEMP);
				free(cp, M_TEMP);
				goto out;
			}
			cp[nfsdarg.mdspathlen] = '\0';	/* Ensure nul term. */
			nfsdarg.mdspath = cp;
		} else {
			nfsdarg.addr = NULL;
			nfsdarg.addrlen = 0;
			nfsdarg.dnshost = NULL;
			nfsdarg.dnshostlen = 0;
			nfsdarg.dspath = NULL;
			nfsdarg.dspathlen = 0;
			nfsdarg.mdspath = NULL;
			nfsdarg.mdspathlen = 0;
			nfsdarg.mirrorcnt = 1;
		}
		error = nfsrvd_nfsd(td, &nfsdarg);
		free(nfsdarg.addr, M_TEMP);
		free(nfsdarg.dnshost, M_TEMP);
		free(nfsdarg.dspath, M_TEMP);
		free(nfsdarg.mdspath, M_TEMP);
	} else if (uap->flag & NFSSVC_PNFSDS) {
		error = copyin(uap->argp, &pnfsdarg, sizeof(pnfsdarg));
		if (error == 0 && (pnfsdarg.op == PNFSDOP_DELDSSERVER ||
		    pnfsdarg.op == PNFSDOP_FORCEDELDS)) {
			cp = malloc(PATH_MAX + 1, M_TEMP, M_WAITOK);
			error = copyinstr(pnfsdarg.dspath, cp, PATH_MAX + 1,
			    NULL);
			if (error == 0)
				error = nfsrv_deldsserver(pnfsdarg.op, cp, td);
			free(cp, M_TEMP);
		} else if (error == 0 && pnfsdarg.op == PNFSDOP_COPYMR) {
			cp = malloc(PATH_MAX + 1, M_TEMP, M_WAITOK);
			buflen = sizeof(*pf) * NFSDEV_MAXMIRRORS;
			buf = malloc(buflen, M_TEMP, M_WAITOK);
			error = copyinstr(pnfsdarg.mdspath, cp, PATH_MAX + 1,
			    NULL);
			NFSD_DEBUG(4, "pnfsdcopymr cp mdspath=%d\n", error);
			if (error == 0 && pnfsdarg.dspath != NULL) {
				cp2 = malloc(PATH_MAX + 1, M_TEMP, M_WAITOK);
				error = copyinstr(pnfsdarg.dspath, cp2,
				    PATH_MAX + 1, NULL);
				NFSD_DEBUG(4, "pnfsdcopymr cp dspath=%d\n",
				    error);
			} else
				cp2 = NULL;
			if (error == 0 && pnfsdarg.curdspath != NULL) {
				cp3 = malloc(PATH_MAX + 1, M_TEMP, M_WAITOK);
				error = copyinstr(pnfsdarg.curdspath, cp3,
				    PATH_MAX + 1, NULL);
				NFSD_DEBUG(4, "pnfsdcopymr cp curdspath=%d\n",
				    error);
			} else
				cp3 = NULL;
			curdvp = NULL;
			fds = NULL;
			if (error == 0)
				error = nfsrv_mdscopymr(cp, cp2, cp3, buf,
				    &buflen, fname, td, &vp, &nvp, &pf, &ds,
				    &fds);
			NFSD_DEBUG(4, "nfsrv_mdscopymr=%d\n", error);
			if (error == 0) {
				if (pf->dsf_dir >= nfsrv_dsdirsize) {
					printf("copymr: dsdir out of range\n");
					pf->dsf_dir = 0;
				}
				NFSD_DEBUG(4, "copymr: buflen=%d\n", buflen);
				error = nfsrv_copymr(vp, nvp,
				    ds->nfsdev_dsdir[pf->dsf_dir], ds, pf,
				    (struct pnfsdsfile *)buf,
				    buflen / sizeof(*pf), td->td_ucred, td);
				vput(vp);
				vput(nvp);
				if (fds != NULL && error == 0) {
					curdvp = fds->nfsdev_dsdir[pf->dsf_dir];
					ret = vn_lock(curdvp, LK_EXCLUSIVE);
					if (ret == 0) {
						nfsrv_dsremove(curdvp, fname,
						    td->td_ucred, td);
						NFSVOPUNLOCK(curdvp, 0);
					}
				}
				NFSD_DEBUG(4, "nfsrv_copymr=%d\n", error);
			}
			free(cp, M_TEMP);
			free(cp2, M_TEMP);
			free(cp3, M_TEMP);
			free(buf, M_TEMP);
		}
	} else {
		error = nfssvc_srvcall(td, uap, td->td_ucred);
	}

out:
	NFSEXITCODE(error);
	return (error);
}

static int
nfssvc_srvcall(struct thread *p, struct nfssvc_args *uap, struct ucred *cred)
{
	struct nfsex_args export;
	struct file *fp = NULL;
	int stablefd, len;
	struct nfsd_clid adminrevoke;
	struct nfsd_dumplist dumplist;
	struct nfsd_dumpclients *dumpclients;
	struct nfsd_dumplocklist dumplocklist;
	struct nfsd_dumplocks *dumplocks;
	struct nameidata nd;
	vnode_t vp;
	int error = EINVAL, igotlock;
	struct proc *procp;
	static int suspend_nfsd = 0;

	if (uap->flag & NFSSVC_PUBLICFH) {
		NFSBZERO((caddr_t)&nfs_pubfh.nfsrvfh_data,
		    sizeof (fhandle_t));
		error = copyin(uap->argp,
		    &nfs_pubfh.nfsrvfh_data, sizeof (fhandle_t));
		if (!error)
			nfs_pubfhset = 1;
	} else if (uap->flag & NFSSVC_V4ROOTEXPORT) {
		error = copyin(uap->argp,(caddr_t)&export,
		    sizeof (struct nfsex_args));
		if (!error)
			error = nfsrv_v4rootexport(&export, cred, p);
	} else if (uap->flag & NFSSVC_NOPUBLICFH) {
		nfs_pubfhset = 0;
		error = 0;
	} else if (uap->flag & NFSSVC_STABLERESTART) {
		error = copyin(uap->argp, (caddr_t)&stablefd,
		    sizeof (int));
		if (!error)
			error = fp_getfvp(p, stablefd, &fp, &vp);
		if (!error && (NFSFPFLAG(fp) & (FREAD | FWRITE)) != (FREAD | FWRITE))
			error = EBADF;
		if (!error && newnfs_numnfsd != 0)
			error = EPERM;
		if (!error) {
			nfsrv_stablefirst.nsf_fp = fp;
			nfsrv_setupstable(p);
		}
	} else if (uap->flag & NFSSVC_ADMINREVOKE) {
		error = copyin(uap->argp, (caddr_t)&adminrevoke,
		    sizeof (struct nfsd_clid));
		if (!error)
			error = nfsrv_adminrevoke(&adminrevoke, p);
	} else if (uap->flag & NFSSVC_DUMPCLIENTS) {
		error = copyin(uap->argp, (caddr_t)&dumplist,
		    sizeof (struct nfsd_dumplist));
		if (!error && (dumplist.ndl_size < 1 ||
			dumplist.ndl_size > NFSRV_MAXDUMPLIST))
			error = EPERM;
		if (!error) {
		    len = sizeof (struct nfsd_dumpclients) * dumplist.ndl_size;
		    dumpclients = malloc(len, M_TEMP, M_WAITOK | M_ZERO);
		    nfsrv_dumpclients(dumpclients, dumplist.ndl_size);
		    error = copyout(dumpclients,
			CAST_USER_ADDR_T(dumplist.ndl_list), len);
		    free(dumpclients, M_TEMP);
		}
	} else if (uap->flag & NFSSVC_DUMPLOCKS) {
		error = copyin(uap->argp, (caddr_t)&dumplocklist,
		    sizeof (struct nfsd_dumplocklist));
		if (!error && (dumplocklist.ndllck_size < 1 ||
			dumplocklist.ndllck_size > NFSRV_MAXDUMPLIST))
			error = EPERM;
		if (!error)
			error = nfsrv_lookupfilename(&nd,
				dumplocklist.ndllck_fname, p);
		if (!error) {
			len = sizeof (struct nfsd_dumplocks) *
				dumplocklist.ndllck_size;
			dumplocks = malloc(len, M_TEMP, M_WAITOK | M_ZERO);
			nfsrv_dumplocks(nd.ni_vp, dumplocks,
			    dumplocklist.ndllck_size, p);
			vput(nd.ni_vp);
			error = copyout(dumplocks,
			    CAST_USER_ADDR_T(dumplocklist.ndllck_list), len);
			free(dumplocks, M_TEMP);
		}
	} else if (uap->flag & NFSSVC_BACKUPSTABLE) {
		procp = p->td_proc;
		PROC_LOCK(procp);
		nfsd_master_pid = procp->p_pid;
		bcopy(procp->p_comm, nfsd_master_comm, MAXCOMLEN + 1);
		nfsd_master_start = procp->p_stats->p_start;
		nfsd_master_proc = procp;
		PROC_UNLOCK(procp);
	} else if ((uap->flag & NFSSVC_SUSPENDNFSD) != 0) {
		NFSLOCKV4ROOTMUTEX();
		if (suspend_nfsd == 0) {
			/* Lock out all nfsd threads */
			do {
				igotlock = nfsv4_lock(&nfsd_suspend_lock, 1,
				    NULL, NFSV4ROOTLOCKMUTEXPTR, NULL);
			} while (igotlock == 0 && suspend_nfsd == 0);
			suspend_nfsd = 1;
		}
		NFSUNLOCKV4ROOTMUTEX();
		error = 0;
	} else if ((uap->flag & NFSSVC_RESUMENFSD) != 0) {
		NFSLOCKV4ROOTMUTEX();
		if (suspend_nfsd != 0) {
			nfsv4_unlock(&nfsd_suspend_lock, 0);
			suspend_nfsd = 0;
		}
		NFSUNLOCKV4ROOTMUTEX();
		error = 0;
	}

	NFSEXITCODE(error);
	return (error);
}

/*
 * Check exports.
 * Returns 0 if ok, 1 otherwise.
 */
int
nfsvno_testexp(struct nfsrv_descript *nd, struct nfsexstuff *exp)
{
	int i;

	/*
	 * This seems odd, but allow the case where the security flavor
	 * list is empty. This happens when NFSv4 is traversing non-exported
	 * file systems. Exported file systems should always have a non-empty
	 * security flavor list.
	 */
	if (exp->nes_numsecflavor == 0)
		return (0);

	for (i = 0; i < exp->nes_numsecflavor; i++) {
		/*
		 * The tests for privacy and integrity must be first,
		 * since ND_GSS is set for everything but AUTH_SYS.
		 */
		if (exp->nes_secflavors[i] == RPCSEC_GSS_KRB5P &&
		    (nd->nd_flag & ND_GSSPRIVACY))
			return (0);
		if (exp->nes_secflavors[i] == RPCSEC_GSS_KRB5I &&
		    (nd->nd_flag & ND_GSSINTEGRITY))
			return (0);
		if (exp->nes_secflavors[i] == RPCSEC_GSS_KRB5 &&
		    (nd->nd_flag & ND_GSS))
			return (0);
		if (exp->nes_secflavors[i] == AUTH_SYS &&
		    (nd->nd_flag & ND_GSS) == 0)
			return (0);
	}
	return (1);
}

/*
 * Calculate a hash value for the fid in a file handle.
 */
uint32_t
nfsrv_hashfh(fhandle_t *fhp)
{
	uint32_t hashval;

	hashval = hash32_buf(&fhp->fh_fid, sizeof(struct fid), 0);
	return (hashval);
}

/*
 * Calculate a hash value for the sessionid.
 */
uint32_t
nfsrv_hashsessionid(uint8_t *sessionid)
{
	uint32_t hashval;

	hashval = hash32_buf(sessionid, NFSX_V4SESSIONID, 0);
	return (hashval);
}

/*
 * Signal the userland master nfsd to backup the stable restart file.
 */
void
nfsrv_backupstable(void)
{
	struct proc *procp;

	if (nfsd_master_proc != NULL) {
		procp = pfind(nfsd_master_pid);
		/* Try to make sure it is the correct process. */
		if (procp == nfsd_master_proc &&
		    procp->p_stats->p_start.tv_sec ==
		    nfsd_master_start.tv_sec &&
		    procp->p_stats->p_start.tv_usec ==
		    nfsd_master_start.tv_usec &&
		    strcmp(procp->p_comm, nfsd_master_comm) == 0)
			kern_psignal(procp, SIGUSR2);
		else
			nfsd_master_proc = NULL;

		if (procp != NULL)
			PROC_UNLOCK(procp);
	}
}

/*
 * Create a DS data file for nfsrv_pnfscreate(). Called for each mirror.
 * The arguments are in a structure, so that they can be passed through
 * taskqueue for a kernel process to execute this function.
 */
struct nfsrvdscreate {
	int			done;
	int			inprog;
	struct task		tsk;
	struct ucred		*tcred;
	struct vnode		*dvp;
	NFSPROC_T		*p;
	struct pnfsdsfile	*pf;
	int			err;
	fhandle_t		fh;
	struct vattr		va;
	struct vattr		createva;
};

int
nfsrv_dscreate(struct vnode *dvp, struct vattr *vap, struct vattr *nvap,
    fhandle_t *fhp, struct pnfsdsfile *pf, struct pnfsdsattr *dsa,
    char *fnamep, struct ucred *tcred, NFSPROC_T *p, struct vnode **nvpp)
{
	struct vnode *nvp;
	struct nameidata named;
	struct vattr va;
	char *bufp;
	u_long *hashp;
	struct nfsnode *np;
	struct nfsmount *nmp;
	int error;

	NFSNAMEICNDSET(&named.ni_cnd, tcred, CREATE,
	    LOCKPARENT | LOCKLEAF | SAVESTART | NOCACHE);
	nfsvno_setpathbuf(&named, &bufp, &hashp);
	named.ni_cnd.cn_lkflags = LK_EXCLUSIVE;
	named.ni_cnd.cn_thread = p;
	named.ni_cnd.cn_nameptr = bufp;
	if (fnamep != NULL) {
		strlcpy(bufp, fnamep, PNFS_FILENAME_LEN + 1);
		named.ni_cnd.cn_namelen = strlen(bufp);
	} else
		named.ni_cnd.cn_namelen = nfsrv_putfhname(fhp, bufp);
	NFSD_DEBUG(4, "nfsrv_dscreate: dvp=%p fname=%s\n", dvp, bufp);

	/* Create the date file in the DS mount. */
	error = NFSVOPLOCK(dvp, LK_EXCLUSIVE);
	if (error == 0) {
		error = VOP_CREATE(dvp, &nvp, &named.ni_cnd, vap);
		NFSVOPUNLOCK(dvp, 0);
		if (error == 0) {
			/* Set the ownership of the file. */
			error = VOP_SETATTR(nvp, nvap, tcred);
			NFSD_DEBUG(4, "nfsrv_dscreate:"
			    " setattr-uid=%d\n", error);
			if (error != 0)
				vput(nvp);
		}
		if (error != 0)
			printf("pNFS: pnfscreate failed=%d\n", error);
	} else
		printf("pNFS: pnfscreate vnlock=%d\n", error);
	if (error == 0) {
		np = VTONFS(nvp);
		nmp = VFSTONFS(nvp->v_mount);
		if (strcmp(nvp->v_mount->mnt_vfc->vfc_name, "nfs")
		    != 0 || nmp->nm_nam->sa_len > sizeof(
		    struct sockaddr_in6) ||
		    np->n_fhp->nfh_len != NFSX_MYFH) {
			printf("Bad DS file: fstype=%s salen=%d"
			    " fhlen=%d\n",
			    nvp->v_mount->mnt_vfc->vfc_name,
			    nmp->nm_nam->sa_len, np->n_fhp->nfh_len);
			error = ENOENT;
		}

		/* Set extattrs for the DS on the MDS file. */
		if (error == 0) {
			if (dsa != NULL) {
				error = VOP_GETATTR(nvp, &va, tcred);
				if (error == 0) {
					dsa->dsa_filerev = va.va_filerev;
					dsa->dsa_size = va.va_size;
					dsa->dsa_atime = va.va_atime;
					dsa->dsa_mtime = va.va_mtime;
				}
			}
			if (error == 0) {
				NFSBCOPY(np->n_fhp->nfh_fh, &pf->dsf_fh,
				    NFSX_MYFH);
				NFSBCOPY(nmp->nm_nam, &pf->dsf_sin,
				    nmp->nm_nam->sa_len);
				NFSBCOPY(named.ni_cnd.cn_nameptr,
				    pf->dsf_filename,
				    sizeof(pf->dsf_filename));
			}
		} else
			printf("pNFS: pnfscreate can't get DS"
			    " attr=%d\n", error);
		if (nvpp != NULL && error == 0)
			*nvpp = nvp;
		else
			vput(nvp);
	}
	nfsvno_relpathbuf(&named);
	return (error);
}

/*
 * Start up the thread that will execute nfsrv_dscreate().
 */
static void
start_dscreate(void *arg, int pending)
{
	struct nfsrvdscreate *dsc;

	dsc = (struct nfsrvdscreate *)arg;
	dsc->err = nfsrv_dscreate(dsc->dvp, &dsc->createva, &dsc->va, &dsc->fh,
	    dsc->pf, NULL, NULL, dsc->tcred, dsc->p, NULL);
	dsc->done = 1;
	NFSD_DEBUG(4, "start_dscreate: err=%d\n", dsc->err);
}

/*
 * Create a pNFS data file on the Data Server(s).
 */
static void
nfsrv_pnfscreate(struct vnode *vp, struct vattr *vap, struct ucred *cred,
    NFSPROC_T *p)
{
	struct nfsrvdscreate *dsc, *tdsc;
	struct nfsdevice *ds, *tds, *fds;
	struct mount *mp;
	struct pnfsdsfile *pf, *tpf;
	struct pnfsdsattr dsattr;
	struct vattr va;
	struct vnode *dvp[NFSDEV_MAXMIRRORS];
	struct nfsmount *nmp;
	fhandle_t fh;
	uid_t vauid;
	gid_t vagid;
	u_short vamode;
	struct ucred *tcred;
	int dsdir[NFSDEV_MAXMIRRORS], error, i, mirrorcnt, ret;
	int failpos, timo;

	/* Get a DS server directory in a round-robin order. */
	mirrorcnt = 1;
	mp = vp->v_mount;
	ds = fds = NULL;
	NFSDDSLOCK();
	/*
	 * Search for the first entry that handles this MDS fs, but use the
	 * first entry for all MDS fs's otherwise.
	 */
	TAILQ_FOREACH(tds, &nfsrv_devidhead, nfsdev_list) {
		if (tds->nfsdev_nmp != NULL) {
			if (tds->nfsdev_mdsisset == 0 && ds == NULL)
				ds = tds;
			else if (tds->nfsdev_mdsisset != 0 &&
			    mp->mnt_stat.f_fsid.val[0] ==
			    tds->nfsdev_mdsfsid.val[0] &&
			    mp->mnt_stat.f_fsid.val[1] ==
			    tds->nfsdev_mdsfsid.val[1]) {
				ds = fds = tds;
				break;
			}
		}
	}
	if (ds == NULL) {
		NFSDDSUNLOCK();
		NFSD_DEBUG(4, "nfsrv_pnfscreate: no srv\n");
		return;
	}
	i = dsdir[0] = ds->nfsdev_nextdir;
	ds->nfsdev_nextdir = (ds->nfsdev_nextdir + 1) % nfsrv_dsdirsize;
	dvp[0] = ds->nfsdev_dsdir[i];
	tds = TAILQ_NEXT(ds, nfsdev_list);
	if (nfsrv_maxpnfsmirror > 1 && tds != NULL) {
		TAILQ_FOREACH_FROM(tds, &nfsrv_devidhead, nfsdev_list) {
			if (tds->nfsdev_nmp != NULL &&
			    ((tds->nfsdev_mdsisset == 0 && fds == NULL) ||
			     (tds->nfsdev_mdsisset != 0 && fds != NULL &&
			      mp->mnt_stat.f_fsid.val[0] ==
			      tds->nfsdev_mdsfsid.val[0] &&
			      mp->mnt_stat.f_fsid.val[1] ==
			      tds->nfsdev_mdsfsid.val[1]))) {
				dsdir[mirrorcnt] = i;
				dvp[mirrorcnt] = tds->nfsdev_dsdir[i];
				mirrorcnt++;
				if (mirrorcnt >= nfsrv_maxpnfsmirror)
					break;
			}
		}
	}
	/* Put at end of list to implement round-robin usage. */
	TAILQ_REMOVE(&nfsrv_devidhead, ds, nfsdev_list);
	TAILQ_INSERT_TAIL(&nfsrv_devidhead, ds, nfsdev_list);
	NFSDDSUNLOCK();
	dsc = NULL;
	if (mirrorcnt > 1)
		tdsc = dsc = malloc(sizeof(*dsc) * (mirrorcnt - 1), M_TEMP,
		    M_WAITOK | M_ZERO);
	tpf = pf = malloc(sizeof(*pf) * nfsrv_maxpnfsmirror, M_TEMP, M_WAITOK |
	    M_ZERO);

	error = nfsvno_getfh(vp, &fh, p);
	if (error == 0)
		error = VOP_GETATTR(vp, &va, cred);
	if (error == 0) {
		/* Set the attributes for "vp" to Setattr the DS vp. */
		vauid = va.va_uid;
		vagid = va.va_gid;
		vamode = va.va_mode;
		VATTR_NULL(&va);
		va.va_uid = vauid;
		va.va_gid = vagid;
		va.va_mode = vamode;
		va.va_size = 0;
	} else
		printf("pNFS: pnfscreate getfh+attr=%d\n", error);

	NFSD_DEBUG(4, "nfsrv_pnfscreate: cruid=%d crgid=%d\n", cred->cr_uid,
	    cred->cr_gid);
	/* Make data file name based on FH. */
	tcred = newnfs_getcred();

	/*
	 * Create the file on each DS mirror, using kernel process(es) for the
	 * additional mirrors.
	 */
	failpos = -1;
	for (i = 0; i < mirrorcnt - 1 && error == 0; i++, tpf++, tdsc++) {
		tpf->dsf_dir = dsdir[i];
		tdsc->tcred = tcred;
		tdsc->p = p;
		tdsc->pf = tpf;
		tdsc->createva = *vap;
		NFSBCOPY(&fh, &tdsc->fh, sizeof(fh));
		tdsc->va = va;
		tdsc->dvp = dvp[i];
		tdsc->done = 0;
		tdsc->inprog = 0;
		tdsc->err = 0;
		ret = EIO;
		if (nfs_pnfsiothreads != 0) {
			ret = nfs_pnfsio(start_dscreate, tdsc);
			NFSD_DEBUG(4, "nfsrv_pnfscreate: nfs_pnfsio=%d\n", ret);
		}
		if (ret != 0) {
			ret = nfsrv_dscreate(dvp[i], vap, &va, &fh, tpf, NULL,
			    NULL, tcred, p, NULL);
			if (ret != 0) {
				KASSERT(error == 0, ("nfsrv_dscreate err=%d",
				    error));
				if (failpos == -1 && nfsds_failerr(ret))
					failpos = i;
				else
					error = ret;
			}
		}
	}
	if (error == 0) {
		tpf->dsf_dir = dsdir[mirrorcnt - 1];
		error = nfsrv_dscreate(dvp[mirrorcnt - 1], vap, &va, &fh, tpf,
		    &dsattr, NULL, tcred, p, NULL);
		if (failpos == -1 && mirrorcnt > 1 && nfsds_failerr(error)) {
			failpos = mirrorcnt - 1;
			error = 0;
		}
	}
	timo = hz / 50;		/* Wait for 20msec. */
	if (timo < 1)
		timo = 1;
	/* Wait for kernel task(s) to complete. */
	for (tdsc = dsc, i = 0; i < mirrorcnt - 1; i++, tdsc++) {
		while (tdsc->inprog != 0 && tdsc->done == 0)
			tsleep(&tdsc->tsk, PVFS, "srvdcr", timo);
		if (tdsc->err != 0) {
			if (failpos == -1 && nfsds_failerr(tdsc->err))
				failpos = i;
			else if (error == 0)
				error = tdsc->err;
		}
	}

	/*
	 * If failpos has been set, that mirror has failed, so it needs
	 * to be disabled.
	 */
	if (failpos >= 0) {
		nmp = VFSTONFS(dvp[failpos]->v_mount);
		NFSLOCKMNT(nmp);
		if ((nmp->nm_privflag & (NFSMNTP_FORCEDISM |
		     NFSMNTP_CANCELRPCS)) == 0) {
			nmp->nm_privflag |= NFSMNTP_CANCELRPCS;
			NFSUNLOCKMNT(nmp);
			ds = nfsrv_deldsnmp(PNFSDOP_DELDSSERVER, nmp, p);
			NFSD_DEBUG(4, "dscreatfail fail=%d ds=%p\n", failpos,
			    ds);
			if (ds != NULL)
				nfsrv_killrpcs(nmp);
			NFSLOCKMNT(nmp);
			nmp->nm_privflag &= ~NFSMNTP_CANCELRPCS;
			wakeup(nmp);
		}
		NFSUNLOCKMNT(nmp);
	}

	NFSFREECRED(tcred);
	if (error == 0) {
		ASSERT_VOP_ELOCKED(vp, "nfsrv_pnfscreate vp");

		NFSD_DEBUG(4, "nfsrv_pnfscreate: mirrorcnt=%d maxmirror=%d\n",
		    mirrorcnt, nfsrv_maxpnfsmirror);
		/*
		 * For all mirrors that couldn't be created, fill in the
		 * *pf structure, but with an IP address == 0.0.0.0.
		 */
		tpf = pf + mirrorcnt;
		for (i = mirrorcnt; i < nfsrv_maxpnfsmirror; i++, tpf++) {
			*tpf = *pf;
			tpf->dsf_sin.sin_family = AF_INET;
			tpf->dsf_sin.sin_len = sizeof(struct sockaddr_in);
			tpf->dsf_sin.sin_addr.s_addr = 0;
			tpf->dsf_sin.sin_port = 0;
		}

		error = vn_extattr_set(vp, IO_NODELOCKED,
		    EXTATTR_NAMESPACE_SYSTEM, "pnfsd.dsfile",
		    sizeof(*pf) * nfsrv_maxpnfsmirror, (char *)pf, p);
		if (error == 0)
			error = vn_extattr_set(vp, IO_NODELOCKED,
			    EXTATTR_NAMESPACE_SYSTEM, "pnfsd.dsattr",
			    sizeof(dsattr), (char *)&dsattr, p);
		if (error != 0)
			printf("pNFS: pnfscreate setextattr=%d\n",
			    error);
	} else
		printf("pNFS: pnfscreate=%d\n", error);
	free(pf, M_TEMP);
	free(dsc, M_TEMP);
}

/*
 * Get the information needed to remove the pNFS Data Server file from the
 * Metadata file.  Upon success, ddvp is set non-NULL to the locked
 * DS directory vnode.  The caller must unlock *ddvp when done with it.
 */
static void
nfsrv_pnfsremovesetup(struct vnode *vp, NFSPROC_T *p, struct vnode **dvpp,
    int *mirrorcntp, char *fname, fhandle_t *fhp)
{
	struct vattr va;
	struct ucred *tcred;
	char *buf;
	int buflen, error;

	dvpp[0] = NULL;
	/* If not an exported regular file or not a pNFS server, just return. */
	if (vp->v_type != VREG || (vp->v_mount->mnt_flag & MNT_EXPORTED) == 0 ||
	    nfsrv_devidcnt == 0)
		return;

	/* Check to see if this is the last hard link. */
	tcred = newnfs_getcred();
	error = VOP_GETATTR(vp, &va, tcred);
	NFSFREECRED(tcred);
	if (error != 0) {
		printf("pNFS: nfsrv_pnfsremovesetup getattr=%d\n", error);
		return;
	}
	if (va.va_nlink > 1)
		return;

	error = nfsvno_getfh(vp, fhp, p);
	if (error != 0) {
		printf("pNFS: nfsrv_pnfsremovesetup getfh=%d\n", error);
		return;
	}

	buflen = 1024;
	buf = malloc(buflen, M_TEMP, M_WAITOK);
	/* Get the directory vnode for the DS mount and the file handle. */
	error = nfsrv_dsgetsockmnt(vp, 0, buf, &buflen, mirrorcntp, p, dvpp,
	    NULL, NULL, fname, NULL, NULL, NULL, NULL, NULL);
	free(buf, M_TEMP);
	if (error != 0)
		printf("pNFS: nfsrv_pnfsremovesetup getsockmnt=%d\n", error);
}

/*
 * Remove a DS data file for nfsrv_pnfsremove(). Called for each mirror.
 * The arguments are in a structure, so that they can be passed through
 * taskqueue for a kernel process to execute this function.
 */
struct nfsrvdsremove {
	int			done;
	int			inprog;
	struct task		tsk;
	struct ucred		*tcred;
	struct vnode		*dvp;
	NFSPROC_T		*p;
	int			err;
	char			fname[PNFS_FILENAME_LEN + 1];
};

static int
nfsrv_dsremove(struct vnode *dvp, char *fname, struct ucred *tcred,
    NFSPROC_T *p)
{
	struct nameidata named;
	struct vnode *nvp;
	char *bufp;
	u_long *hashp;
	int error;

	error = NFSVOPLOCK(dvp, LK_EXCLUSIVE);
	if (error != 0)
		return (error);
	named.ni_cnd.cn_nameiop = DELETE;
	named.ni_cnd.cn_lkflags = LK_EXCLUSIVE | LK_RETRY;
	named.ni_cnd.cn_cred = tcred;
	named.ni_cnd.cn_thread = p;
	named.ni_cnd.cn_flags = ISLASTCN | LOCKPARENT | LOCKLEAF | SAVENAME;
	nfsvno_setpathbuf(&named, &bufp, &hashp);
	named.ni_cnd.cn_nameptr = bufp;
	named.ni_cnd.cn_namelen = strlen(fname);
	strlcpy(bufp, fname, NAME_MAX);
	NFSD_DEBUG(4, "nfsrv_pnfsremove: filename=%s\n", bufp);
	error = VOP_LOOKUP(dvp, &nvp, &named.ni_cnd);
	NFSD_DEBUG(4, "nfsrv_pnfsremove: aft LOOKUP=%d\n", error);
	if (error == 0) {
		error = VOP_REMOVE(dvp, nvp, &named.ni_cnd);
		vput(nvp);
	}
	NFSVOPUNLOCK(dvp, 0);
	nfsvno_relpathbuf(&named);
	if (error != 0)
		printf("pNFS: nfsrv_pnfsremove failed=%d\n", error);
	return (error);
}

/*
 * Start up the thread that will execute nfsrv_dsremove().
 */
static void
start_dsremove(void *arg, int pending)
{
	struct nfsrvdsremove *dsrm;

	dsrm = (struct nfsrvdsremove *)arg;
	dsrm->err = nfsrv_dsremove(dsrm->dvp, dsrm->fname, dsrm->tcred,
	    dsrm->p);
	dsrm->done = 1;
	NFSD_DEBUG(4, "start_dsremove: err=%d\n", dsrm->err);
}

/*
 * Remove a pNFS data file from a Data Server.
 * nfsrv_pnfsremovesetup() must have been called before the MDS file was
 * removed to set up the dvp and fill in the FH.
 */
static void
nfsrv_pnfsremove(struct vnode **dvp, int mirrorcnt, char *fname, fhandle_t *fhp,
    NFSPROC_T *p)
{
	struct ucred *tcred;
	struct nfsrvdsremove *dsrm, *tdsrm;
	struct nfsdevice *ds;
	struct nfsmount *nmp;
	int failpos, i, ret, timo;

	tcred = newnfs_getcred();
	dsrm = NULL;
	if (mirrorcnt > 1)
		dsrm = malloc(sizeof(*dsrm) * mirrorcnt - 1, M_TEMP, M_WAITOK);
	/*
	 * Remove the file on each DS mirror, using kernel process(es) for the
	 * additional mirrors.
	 */
	failpos = -1;
	for (tdsrm = dsrm, i = 0; i < mirrorcnt - 1; i++, tdsrm++) {
		tdsrm->tcred = tcred;
		tdsrm->p = p;
		tdsrm->dvp = dvp[i];
		strlcpy(tdsrm->fname, fname, PNFS_FILENAME_LEN + 1);
		tdsrm->inprog = 0;
		tdsrm->done = 0;
		tdsrm->err = 0;
		ret = EIO;
		if (nfs_pnfsiothreads != 0) {
			ret = nfs_pnfsio(start_dsremove, tdsrm);
			NFSD_DEBUG(4, "nfsrv_pnfsremove: nfs_pnfsio=%d\n", ret);
		}
		if (ret != 0) {
			ret = nfsrv_dsremove(dvp[i], fname, tcred, p);
			if (failpos == -1 && nfsds_failerr(ret))
				failpos = i;
		}
	}
	ret = nfsrv_dsremove(dvp[mirrorcnt - 1], fname, tcred, p);
	if (failpos == -1 && mirrorcnt > 1 && nfsds_failerr(ret))
		failpos = mirrorcnt - 1;
	timo = hz / 50;		/* Wait for 20msec. */
	if (timo < 1)
		timo = 1;
	/* Wait for kernel task(s) to complete. */
	for (tdsrm = dsrm, i = 0; i < mirrorcnt - 1; i++, tdsrm++) {
		while (tdsrm->inprog != 0 && tdsrm->done == 0)
			tsleep(&tdsrm->tsk, PVFS, "srvdsrm", timo);
		if (failpos == -1 && nfsds_failerr(tdsrm->err))
			failpos = i;
	}

	/*
	 * If failpos has been set, that mirror has failed, so it needs
	 * to be disabled.
	 */
	if (failpos >= 0) {
		nmp = VFSTONFS(dvp[failpos]->v_mount);
		NFSLOCKMNT(nmp);
		if ((nmp->nm_privflag & (NFSMNTP_FORCEDISM |
		     NFSMNTP_CANCELRPCS)) == 0) {
			nmp->nm_privflag |= NFSMNTP_CANCELRPCS;
			NFSUNLOCKMNT(nmp);
			ds = nfsrv_deldsnmp(PNFSDOP_DELDSSERVER, nmp, p);
			NFSD_DEBUG(4, "dsremovefail fail=%d ds=%p\n", failpos,
			    ds);
			if (ds != NULL)
				nfsrv_killrpcs(nmp);
			NFSLOCKMNT(nmp);
			nmp->nm_privflag &= ~NFSMNTP_CANCELRPCS;
			wakeup(nmp);
		}
		NFSUNLOCKMNT(nmp);
	}

	/* Get rid all layouts for the file. */
	nfsrv_freefilelayouts(fhp);

	NFSFREECRED(tcred);
	free(dsrm, M_TEMP);
}

/*
 * Generate a file name based on the file handle and put it in *bufp.
 * Return the number of bytes generated.
 */
static int
nfsrv_putfhname(fhandle_t *fhp, char *bufp)
{
	int i;
	uint8_t *cp;
	const uint8_t *hexdigits = "0123456789abcdef";

	cp = (uint8_t *)fhp;
	for (i = 0; i < sizeof(*fhp); i++) {
		bufp[2 * i] = hexdigits[(*cp >> 4) & 0xf];
		bufp[2 * i + 1] = hexdigits[*cp++ & 0xf];
	}
	bufp[2 * i] = '\0';
	return (2 * i);
}

/*
 * Update the Metadata file's attributes from the DS file when a Read/Write
 * layout is returned.
 * Basically just call nfsrv_proxyds() with procedure == NFSPROC_LAYOUTRETURN
 * so that it does a nfsrv_getattrdsrpc() and nfsrv_setextattr() on the DS file.
 */
int
nfsrv_updatemdsattr(struct vnode *vp, struct nfsvattr *nap, NFSPROC_T *p)
{
	struct ucred *tcred;
	int error;

	/* Do this as root so that it won't fail with EACCES. */
	tcred = newnfs_getcred();
	error = nfsrv_proxyds(NULL, vp, 0, 0, tcred, p, NFSPROC_LAYOUTRETURN,
	    NULL, NULL, NULL, nap, NULL);
	NFSFREECRED(tcred);
	return (error);
}

/*
 * Set the NFSv4 ACL on the DS file to the same ACL as the MDS file.
 */
static int
nfsrv_dssetacl(struct vnode *vp, struct acl *aclp, struct ucred *cred,
    NFSPROC_T *p)
{
	int error;

	error = nfsrv_proxyds(NULL, vp, 0, 0, cred, p, NFSPROC_SETACL,
	    NULL, NULL, NULL, NULL, aclp);
	return (error);
}

static int
nfsrv_proxyds(struct nfsrv_descript *nd, struct vnode *vp, off_t off, int cnt,
    struct ucred *cred, struct thread *p, int ioproc, struct mbuf **mpp,
    char *cp, struct mbuf **mpp2, struct nfsvattr *nap, struct acl *aclp)
{
	struct nfsmount *nmp[NFSDEV_MAXMIRRORS], *failnmp;
	fhandle_t fh[NFSDEV_MAXMIRRORS];
	struct vnode *dvp[NFSDEV_MAXMIRRORS];
	struct nfsdevice *ds;
	struct pnfsdsattr dsattr;
	char *buf;
	int buflen, error, failpos, i, mirrorcnt, origmircnt, trycnt;

	NFSD_DEBUG(4, "in nfsrv_proxyds\n");
	/*
	 * If not a regular file, not exported or not a pNFS server,
	 * just return ENOENT.
	 */
	if (vp->v_type != VREG || (vp->v_mount->mnt_flag & MNT_EXPORTED) == 0 ||
	    nfsrv_devidcnt == 0)
		return (ENOENT);

	buflen = 1024;
	buf = malloc(buflen, M_TEMP, M_WAITOK);
	error = 0;

	/*
	 * For Getattr, get the Change attribute (va_filerev) and size (va_size)
	 * from the MetaData file's extended attribute.
	 */
	if (ioproc == NFSPROC_GETATTR) {
		error = vn_extattr_get(vp, IO_NODELOCKED,
		    EXTATTR_NAMESPACE_SYSTEM, "pnfsd.dsattr", &buflen, buf,
		    p);
		if (error == 0 && buflen != sizeof(dsattr))
			error = ENXIO;
		if (error == 0) {
			NFSBCOPY(buf, &dsattr, buflen);
			nap->na_filerev = dsattr.dsa_filerev;
			nap->na_size = dsattr.dsa_size;
			nap->na_atime = dsattr.dsa_atime;
			nap->na_mtime = dsattr.dsa_mtime;

			/*
			 * If nfsrv_pnfsgetdsattr is 0 or nfsrv_checkdsattr()
			 * returns 0, just return now.  nfsrv_checkdsattr()
			 * returns 0 if there is no Read/Write layout
			 * plus either an Open/Write_access or Write
			 * delegation issued to a client for the file.
			 */
			if (nfsrv_pnfsgetdsattr == 0 ||
			    nfsrv_checkdsattr(nd, vp, p) == 0) {
				free(buf, M_TEMP);
				return (error);
			}
		}

		/*
		 * Clear ENOATTR so the code below will attempt to do a
		 * nfsrv_getattrdsrpc() to get the attributes and (re)create
		 * the extended attribute.
		 */
		if (error == ENOATTR)
			error = 0;
	}

	origmircnt = -1;
	trycnt = 0;
tryagain:
	if (error == 0) {
		buflen = 1024;
		if (ioproc == NFSPROC_READDS && NFSVOPISLOCKED(vp) ==
		    LK_EXCLUSIVE)
			printf("nfsrv_proxyds: Readds vp exclusively locked\n");
		error = nfsrv_dsgetsockmnt(vp, LK_SHARED, buf, &buflen,
		    &mirrorcnt, p, dvp, fh, NULL, NULL, NULL, NULL, NULL,
		    NULL, NULL);
		if (error == 0) {
			for (i = 0; i < mirrorcnt; i++)
				nmp[i] = VFSTONFS(dvp[i]->v_mount);
		} else
			printf("pNFS: proxy getextattr sockaddr=%d\n", error);
	} else
		printf("pNFS: nfsrv_dsgetsockmnt=%d\n", error);
	if (error == 0) {
		failpos = -1;
		if (origmircnt == -1)
			origmircnt = mirrorcnt;
		/*
		 * If failpos is set to a mirror#, then that mirror has
		 * failed and will be disabled. For Read and Getattr, the
		 * function only tries one mirror, so if that mirror has
		 * failed, it will need to be retried. As such, increment
		 * tryitagain for these cases.
		 * For Write, Setattr and Setacl, the function tries all
		 * mirrors and will not return an error for the case where
		 * one mirror has failed. For these cases, the functioning
		 * mirror(s) will have been modified, so a retry isn't
		 * necessary. These functions will set failpos for the
		 * failed mirror#.
		 */
		if (ioproc == NFSPROC_READDS) {
			error = nfsrv_readdsrpc(fh, off, cnt, cred, p, nmp[0],
			    mpp, mpp2);
			if (nfsds_failerr(error) && mirrorcnt > 1) {
				/*
				 * Setting failpos will cause the mirror
				 * to be disabled and then a retry of this
				 * read is required.
				 */
				failpos = 0;
				error = 0;
				trycnt++;
			}
		} else if (ioproc == NFSPROC_WRITEDS)
			error = nfsrv_writedsrpc(fh, off, cnt, cred, p, vp,
			    &nmp[0], mirrorcnt, mpp, cp, &failpos);
		else if (ioproc == NFSPROC_SETATTR)
			error = nfsrv_setattrdsrpc(fh, cred, p, vp, &nmp[0],
			    mirrorcnt, nap, &failpos);
		else if (ioproc == NFSPROC_SETACL)
			error = nfsrv_setacldsrpc(fh, cred, p, vp, &nmp[0],
			    mirrorcnt, aclp, &failpos);
		else {
			error = nfsrv_getattrdsrpc(&fh[mirrorcnt - 1], cred, p,
			    vp, nmp[mirrorcnt - 1], nap);
			if (nfsds_failerr(error) && mirrorcnt > 1) {
				/*
				 * Setting failpos will cause the mirror
				 * to be disabled and then a retry of this
				 * getattr is required.
				 */
				failpos = mirrorcnt - 1;
				error = 0;
				trycnt++;
			}
		}
		ds = NULL;
		if (failpos >= 0) {
			failnmp = nmp[failpos];
			NFSLOCKMNT(failnmp);
			if ((failnmp->nm_privflag & (NFSMNTP_FORCEDISM |
			     NFSMNTP_CANCELRPCS)) == 0) {
				failnmp->nm_privflag |= NFSMNTP_CANCELRPCS;
				NFSUNLOCKMNT(failnmp);
				ds = nfsrv_deldsnmp(PNFSDOP_DELDSSERVER,
				    failnmp, p);
				NFSD_DEBUG(4, "dsldsnmp fail=%d ds=%p\n",
				    failpos, ds);
				if (ds != NULL)
					nfsrv_killrpcs(failnmp);
				NFSLOCKMNT(failnmp);
				failnmp->nm_privflag &= ~NFSMNTP_CANCELRPCS;
				wakeup(failnmp);
			}
			NFSUNLOCKMNT(failnmp);
		}
		for (i = 0; i < mirrorcnt; i++)
			NFSVOPUNLOCK(dvp[i], 0);
		NFSD_DEBUG(4, "nfsrv_proxyds: aft RPC=%d trya=%d\n", error,
		    trycnt);
		/* Try the Read/Getattr again if a mirror was deleted. */
		if (ds != NULL && trycnt > 0 && trycnt < origmircnt)
			goto tryagain;
	} else {
		/* Return ENOENT for any Extended Attribute error. */
		error = ENOENT;
	}
	free(buf, M_TEMP);
	NFSD_DEBUG(4, "nfsrv_proxyds: error=%d\n", error);
	return (error);
}

/*
 * Get the DS mount point, fh and directory from the "pnfsd.dsfile" extended
 * attribute.
 * newnmpp - If it points to a non-NULL nmp, that is the destination and needs
 *           to be checked.  If it points to a NULL nmp, then it returns
 *           a suitable destination.
 * curnmp - If non-NULL, it is the source mount for the copy.
 */
int
nfsrv_dsgetsockmnt(struct vnode *vp, int lktype, char *buf, int *buflenp,
    int *mirrorcntp, NFSPROC_T *p, struct vnode **dvpp, fhandle_t *fhp,
    char *devid, char *fnamep, struct vnode **nvpp, struct nfsmount **newnmpp,
    struct nfsmount *curnmp, int *ippos, int *dsdirp)
{
	struct vnode *dvp, *nvp, **tdvpp;
	struct mount *mp;
	struct nfsmount *nmp, *newnmp;
	struct sockaddr *sad;
	struct sockaddr_in *sin;
	struct nfsdevice *ds, *tds, *fndds;
	struct pnfsdsfile *pf;
	uint32_t dsdir;
	int error, fhiszero, fnd, gotone, i, mirrorcnt;

	ASSERT_VOP_LOCKED(vp, "nfsrv_dsgetsockmnt vp");
	*mirrorcntp = 1;
	tdvpp = dvpp;
	if (nvpp != NULL)
		*nvpp = NULL;
	if (dvpp != NULL)
		*dvpp = NULL;
	if (ippos != NULL)
		*ippos = -1;
	if (newnmpp != NULL)
		newnmp = *newnmpp;
	else
		newnmp = NULL;
	mp = vp->v_mount;
	error = vn_extattr_get(vp, IO_NODELOCKED, EXTATTR_NAMESPACE_SYSTEM,
	    "pnfsd.dsfile", buflenp, buf, p);
	mirrorcnt = *buflenp / sizeof(*pf);
	if (error == 0 && (mirrorcnt < 1 || mirrorcnt > NFSDEV_MAXMIRRORS ||
	    *buflenp != sizeof(*pf) * mirrorcnt))
		error = ENOATTR;

	pf = (struct pnfsdsfile *)buf;
	/* If curnmp != NULL, check for a match in the mirror list. */
	if (curnmp != NULL && error == 0) {
		fnd = 0;
		for (i = 0; i < mirrorcnt; i++, pf++) {
			sad = (struct sockaddr *)&pf->dsf_sin;
			if (nfsaddr2_match(sad, curnmp->nm_nam)) {
				if (ippos != NULL)
					*ippos = i;
				fnd = 1;
				break;
			}
		}
		if (fnd == 0)
			error = ENXIO;
	}

	gotone = 0;
	pf = (struct pnfsdsfile *)buf;
	NFSD_DEBUG(4, "nfsrv_dsgetsockmnt: mirrorcnt=%d err=%d\n", mirrorcnt,
	    error);
	for (i = 0; i < mirrorcnt && error == 0; i++, pf++) {
		fhiszero = 0;
		sad = (struct sockaddr *)&pf->dsf_sin;
		sin = &pf->dsf_sin;
		dsdir = pf->dsf_dir;
		if (dsdir >= nfsrv_dsdirsize) {
			printf("nfsrv_dsgetsockmnt: dsdir=%d\n", dsdir);
			error = ENOATTR;
		} else if (nvpp != NULL && newnmp != NULL &&
		    nfsaddr2_match(sad, newnmp->nm_nam))
			error = EEXIST;
		if (error == 0) {
			if (ippos != NULL && curnmp == NULL &&
			    sad->sa_family == AF_INET &&
			    sin->sin_addr.s_addr == 0)
				*ippos = i;
			if (NFSBCMP(&zerofh, &pf->dsf_fh, sizeof(zerofh)) == 0)
				fhiszero = 1;
			/* Use the socket address to find the mount point. */
			fndds = NULL;
			NFSDDSLOCK();
			/* Find a match for the IP address. */
			TAILQ_FOREACH(ds, &nfsrv_devidhead, nfsdev_list) {
				if (ds->nfsdev_nmp != NULL) {
					dvp = ds->nfsdev_dvp;
					nmp = VFSTONFS(dvp->v_mount);
					if (nmp != ds->nfsdev_nmp)
						printf("different2 nmp %p %p\n",
						    nmp, ds->nfsdev_nmp);
					if (nfsaddr2_match(sad, nmp->nm_nam)) {
						fndds = ds;
						break;
					}
				}
			}
			if (fndds != NULL && newnmpp != NULL &&
			    newnmp == NULL) {
				/* Search for a place to make a mirror copy. */
				TAILQ_FOREACH(tds, &nfsrv_devidhead,
				    nfsdev_list) {
					if (tds->nfsdev_nmp != NULL &&
					    fndds != tds &&
					    ((tds->nfsdev_mdsisset == 0 &&
					      fndds->nfsdev_mdsisset == 0) ||
					     (tds->nfsdev_mdsisset != 0 &&
					      fndds->nfsdev_mdsisset != 0 &&
					      tds->nfsdev_mdsfsid.val[0] ==
					      mp->mnt_stat.f_fsid.val[0] &&
					      tds->nfsdev_mdsfsid.val[1] ==
					      mp->mnt_stat.f_fsid.val[1]))) {
						*newnmpp = tds->nfsdev_nmp;
						break;
					}
				}
				if (tds != NULL) {
					/*
					 * Move this entry to the end of the
					 * list, so it won't be selected as
					 * easily the next time.
					 */
					TAILQ_REMOVE(&nfsrv_devidhead, tds,
					    nfsdev_list);
					TAILQ_INSERT_TAIL(&nfsrv_devidhead, tds,
					    nfsdev_list);
				}
			}
			NFSDDSUNLOCK();
			if (fndds != NULL) {
				dvp = fndds->nfsdev_dsdir[dsdir];
				if (lktype != 0 || fhiszero != 0 ||
				    (nvpp != NULL && *nvpp == NULL)) {
					if (fhiszero != 0)
						error = vn_lock(dvp,
						    LK_EXCLUSIVE);
					else if (lktype != 0)
						error = vn_lock(dvp, lktype);
					else
						error = vn_lock(dvp, LK_SHARED);
					/*
					 * If the file handle is all 0's, try to
					 * do a Lookup against the DS to acquire
					 * it.
					 * If dvpp == NULL or the Lookup fails,
					 * unlock dvp after the call.
					 */
					if (error == 0 && (fhiszero != 0 ||
					    (nvpp != NULL && *nvpp == NULL))) {
						error = nfsrv_pnfslookupds(vp,
						    dvp, pf, &nvp, p);
						if (error == 0) {
							if (fhiszero != 0)
								nfsrv_pnfssetfh(
								    vp, pf,
								    devid,
								    fnamep,
								    nvp, p);
							if (nvpp != NULL &&
							    *nvpp == NULL) {
								*nvpp = nvp;
								*dsdirp = dsdir;
							} else
								vput(nvp);
						}
						if (error != 0 || lktype == 0)
							NFSVOPUNLOCK(dvp, 0);
					}
				}
				if (error == 0) {
					gotone++;
					NFSD_DEBUG(4, "gotone=%d\n", gotone);
					if (devid != NULL) {
						NFSBCOPY(fndds->nfsdev_deviceid,
						    devid, NFSX_V4DEVICEID);
						devid += NFSX_V4DEVICEID;
					}
					if (dvpp != NULL)
						*tdvpp++ = dvp;
					if (fhp != NULL)
						NFSBCOPY(&pf->dsf_fh, fhp++,
						    NFSX_MYFH);
					if (fnamep != NULL && gotone == 1)
						strlcpy(fnamep,
						    pf->dsf_filename,
						    sizeof(pf->dsf_filename));
				} else
					NFSD_DEBUG(4, "nfsrv_dsgetsockmnt "
					    "err=%d\n", error);
			}
		}
	}
	if (error == 0 && gotone == 0)
		error = ENOENT;

	NFSD_DEBUG(4, "eo nfsrv_dsgetsockmnt: gotone=%d err=%d\n", gotone,
	    error);
	if (error == 0)
		*mirrorcntp = gotone;
	else {
		if (gotone > 0 && dvpp != NULL) {
			/*
			 * If the error didn't occur on the first one and
			 * dvpp != NULL, the one(s) prior to the failure will
			 * have locked dvp's that need to be unlocked.
			 */
			for (i = 0; i < gotone; i++) {
				NFSVOPUNLOCK(*dvpp, 0);
				*dvpp++ = NULL;
			}
		}
		/*
		 * If it found the vnode to be copied from before a failure,
		 * it needs to be vput()'d.
		 */
		if (nvpp != NULL && *nvpp != NULL) {
			vput(*nvpp);
			*nvpp = NULL;
		}
	}
	return (error);
}

/*
 * Set the extended attribute for the Change attribute.
 */
static int
nfsrv_setextattr(struct vnode *vp, struct nfsvattr *nap, NFSPROC_T *p)
{
	struct pnfsdsattr dsattr;
	int error;

	ASSERT_VOP_ELOCKED(vp, "nfsrv_setextattr vp");
	dsattr.dsa_filerev = nap->na_filerev;
	dsattr.dsa_size = nap->na_size;
	dsattr.dsa_atime = nap->na_atime;
	dsattr.dsa_mtime = nap->na_mtime;
	error = vn_extattr_set(vp, IO_NODELOCKED, EXTATTR_NAMESPACE_SYSTEM,
	    "pnfsd.dsattr", sizeof(dsattr), (char *)&dsattr, p);
	if (error != 0)
		printf("pNFS: setextattr=%d\n", error);
	return (error);
}

static int
nfsrv_readdsrpc(fhandle_t *fhp, off_t off, int len, struct ucred *cred,
    NFSPROC_T *p, struct nfsmount *nmp, struct mbuf **mpp, struct mbuf **mpendp)
{
	uint32_t *tl;
	struct nfsrv_descript *nd;
	nfsv4stateid_t st;
	struct mbuf *m, *m2;
	int error = 0, retlen, tlen, trimlen;

	NFSD_DEBUG(4, "in nfsrv_readdsrpc\n");
	nd = malloc(sizeof(*nd), M_TEMP, M_WAITOK | M_ZERO);
	*mpp = NULL;
	/*
	 * Use a stateid where other is an alternating 01010 pattern and
	 * seqid is 0xffffffff.  This value is not defined as special by
	 * the RFC and is used by the FreeBSD NFS server to indicate an
	 * MDS->DS proxy operation.
	 */
	st.other[0] = 0x55555555;
	st.other[1] = 0x55555555;
	st.other[2] = 0x55555555;
	st.seqid = 0xffffffff;
	nfscl_reqstart(nd, NFSPROC_READDS, nmp, (u_int8_t *)fhp, sizeof(*fhp),
	    NULL, NULL, 0, 0);
	nfsm_stateidtom(nd, &st, NFSSTATEID_PUTSTATEID);
	NFSM_BUILD(tl, uint32_t *, NFSX_UNSIGNED * 3);
	txdr_hyper(off, tl);
	*(tl + 2) = txdr_unsigned(len);
	error = newnfs_request(nd, nmp, NULL, &nmp->nm_sockreq, NULL, p, cred,
	    NFS_PROG, NFS_VER4, NULL, 1, NULL, NULL);
	if (error != 0) {
		free(nd, M_TEMP);
		return (error);
	}
	if (nd->nd_repstat == 0) {
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		NFSM_STRSIZ(retlen, len);
		if (retlen > 0) {
			/* Trim off the pre-data XDR from the mbuf chain. */
			m = nd->nd_mrep;
			while (m != NULL && m != nd->nd_md) {
				if (m->m_next == nd->nd_md) {
					m->m_next = NULL;
					m_freem(nd->nd_mrep);
					nd->nd_mrep = m = nd->nd_md;
				} else
					m = m->m_next;
			}
			if (m == NULL) {
				printf("nfsrv_readdsrpc: busted mbuf list\n");
				error = ENOENT;
				goto nfsmout;
			}
	
			/*
			 * Now, adjust first mbuf so that any XDR before the
			 * read data is skipped over.
			 */
			trimlen = nd->nd_dpos - mtod(m, char *);
			if (trimlen > 0) {
				m->m_len -= trimlen;
				NFSM_DATAP(m, trimlen);
			}
	
			/*
			 * Truncate the mbuf chain at retlen bytes of data,
			 * plus XDR padding that brings the length up to a
			 * multiple of 4.
			 */
			tlen = NFSM_RNDUP(retlen);
			do {
				if (m->m_len >= tlen) {
					m->m_len = tlen;
					tlen = 0;
					m2 = m->m_next;
					m->m_next = NULL;
					m_freem(m2);
					break;
				}
				tlen -= m->m_len;
				m = m->m_next;
			} while (m != NULL);
			if (tlen > 0) {
				printf("nfsrv_readdsrpc: busted mbuf list\n");
				error = ENOENT;
				goto nfsmout;
			}
			*mpp = nd->nd_mrep;
			*mpendp = m;
			nd->nd_mrep = NULL;
		}
	} else
		error = nd->nd_repstat;
nfsmout:
	/* If nd->nd_mrep is already NULL, this is a no-op. */
	m_freem(nd->nd_mrep);
	free(nd, M_TEMP);
	NFSD_DEBUG(4, "nfsrv_readdsrpc error=%d\n", error);
	return (error);
}

/*
 * Do a write RPC on a DS data file, using this structure for the arguments,
 * so that this function can be executed by a separate kernel process.
 */
struct nfsrvwritedsdorpc {
	int			done;
	int			inprog;
	struct task		tsk;
	fhandle_t		fh;
	off_t			off;
	int			len;
	struct nfsmount		*nmp;
	struct ucred		*cred;
	NFSPROC_T		*p;
	struct mbuf		*m;
	int			err;
};

static int
nfsrv_writedsdorpc(struct nfsmount *nmp, fhandle_t *fhp, off_t off, int len,
    struct nfsvattr *nap, struct mbuf *m, struct ucred *cred, NFSPROC_T *p)
{
	uint32_t *tl;
	struct nfsrv_descript *nd;
	nfsattrbit_t attrbits;
	nfsv4stateid_t st;
	int commit, error, retlen;

	nd = malloc(sizeof(*nd), M_TEMP, M_WAITOK | M_ZERO);
	nfscl_reqstart(nd, NFSPROC_WRITE, nmp, (u_int8_t *)fhp,
	    sizeof(fhandle_t), NULL, NULL, 0, 0);

	/*
	 * Use a stateid where other is an alternating 01010 pattern and
	 * seqid is 0xffffffff.  This value is not defined as special by
	 * the RFC and is used by the FreeBSD NFS server to indicate an
	 * MDS->DS proxy operation.
	 */
	st.other[0] = 0x55555555;
	st.other[1] = 0x55555555;
	st.other[2] = 0x55555555;
	st.seqid = 0xffffffff;
	nfsm_stateidtom(nd, &st, NFSSTATEID_PUTSTATEID);
	NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER + 2 * NFSX_UNSIGNED);
	txdr_hyper(off, tl);
	tl += 2;
	/*
	 * Do all writes FileSync, since the server doesn't hold onto dirty
	 * buffers.  Since clients should be accessing the DS servers directly
	 * using the pNFS layouts, this just needs to work correctly as a
	 * fallback.
	 */
	*tl++ = txdr_unsigned(NFSWRITE_FILESYNC);
	*tl = txdr_unsigned(len);
	NFSD_DEBUG(4, "nfsrv_writedsdorpc: len=%d\n", len);

	/* Put data in mbuf chain. */
	nd->nd_mb->m_next = m;

	/* Set nd_mb and nd_bpos to end of data. */
	while (m->m_next != NULL)
		m = m->m_next;
	nd->nd_mb = m;
	nd->nd_bpos = mtod(m, char *) + m->m_len;
	NFSD_DEBUG(4, "nfsrv_writedsdorpc: lastmb len=%d\n", m->m_len);

	/* Do a Getattr for Size, Change and Modify Time. */
	NFSZERO_ATTRBIT(&attrbits);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_SIZE);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_CHANGE);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_TIMEACCESS);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_TIMEMODIFY);
	NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
	*tl = txdr_unsigned(NFSV4OP_GETATTR);
	(void) nfsrv_putattrbit(nd, &attrbits);
	error = newnfs_request(nd, nmp, NULL, &nmp->nm_sockreq, NULL, p,
	    cred, NFS_PROG, NFS_VER4, NULL, 1, NULL, NULL);
	if (error != 0) {
		free(nd, M_TEMP);
		return (error);
	}
	NFSD_DEBUG(4, "nfsrv_writedsdorpc: aft writerpc=%d\n", nd->nd_repstat);
	/* Get rid of weak cache consistency data for now. */
	if ((nd->nd_flag & (ND_NOMOREDATA | ND_NFSV4 | ND_V4WCCATTR)) ==
	    (ND_NFSV4 | ND_V4WCCATTR)) {
		error = nfsv4_loadattr(nd, NULL, nap, NULL, NULL, 0, NULL, NULL,
		    NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL);
		NFSD_DEBUG(4, "nfsrv_writedsdorpc: wcc attr=%d\n", error);
		if (error != 0)
			goto nfsmout;
		/*
		 * Get rid of Op# and status for next op.
		 */
		NFSM_DISSECT(tl, uint32_t *, 2 * NFSX_UNSIGNED);
		if (*++tl != 0)
			nd->nd_flag |= ND_NOMOREDATA;
	}
	if (nd->nd_repstat == 0) {
		NFSM_DISSECT(tl, uint32_t *, 2 * NFSX_UNSIGNED + NFSX_VERF);
		retlen = fxdr_unsigned(int, *tl++);
		commit = fxdr_unsigned(int, *tl);
		if (commit != NFSWRITE_FILESYNC)
			error = NFSERR_IO;
		NFSD_DEBUG(4, "nfsrv_writedsdorpc:retlen=%d commit=%d err=%d\n",
		    retlen, commit, error);
	} else
		error = nd->nd_repstat;
	/* We have no use for the Write Verifier since we use FileSync. */

	/*
	 * Get the Change, Size, Access Time and Modify Time attributes and set
	 * on the Metadata file, so its attributes will be what the file's
	 * would be if it had been written.
	 */
	if (error == 0) {
		NFSM_DISSECT(tl, uint32_t *, 2 * NFSX_UNSIGNED);
		error = nfsv4_loadattr(nd, NULL, nap, NULL, NULL, 0, NULL, NULL,
		    NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL);
	}
	NFSD_DEBUG(4, "nfsrv_writedsdorpc: aft loadattr=%d\n", error);
nfsmout:
	m_freem(nd->nd_mrep);
	free(nd, M_TEMP);
	NFSD_DEBUG(4, "nfsrv_writedsdorpc error=%d\n", error);
	return (error);
}

/*
 * Start up the thread that will execute nfsrv_writedsdorpc().
 */
static void
start_writedsdorpc(void *arg, int pending)
{
	struct nfsrvwritedsdorpc *drpc;

	drpc = (struct nfsrvwritedsdorpc *)arg;
	drpc->err = nfsrv_writedsdorpc(drpc->nmp, &drpc->fh, drpc->off,
	    drpc->len, NULL, drpc->m, drpc->cred, drpc->p);
	drpc->done = 1;
	NFSD_DEBUG(4, "start_writedsdorpc: err=%d\n", drpc->err);
}

static int
nfsrv_writedsrpc(fhandle_t *fhp, off_t off, int len, struct ucred *cred,
    NFSPROC_T *p, struct vnode *vp, struct nfsmount **nmpp, int mirrorcnt,
    struct mbuf **mpp, char *cp, int *failposp)
{
	struct nfsrvwritedsdorpc *drpc, *tdrpc;
	struct nfsvattr na;
	struct mbuf *m;
	int error, i, offs, ret, timo;

	NFSD_DEBUG(4, "in nfsrv_writedsrpc\n");
	KASSERT(*mpp != NULL, ("nfsrv_writedsrpc: NULL mbuf chain"));
	drpc = NULL;
	if (mirrorcnt > 1)
		tdrpc = drpc = malloc(sizeof(*drpc) * (mirrorcnt - 1), M_TEMP,
		    M_WAITOK);

	/* Calculate offset in mbuf chain that data starts. */
	offs = cp - mtod(*mpp, char *);
	NFSD_DEBUG(4, "nfsrv_writedsrpc: mcopy offs=%d len=%d\n", offs, len);

	/*
	 * Do the write RPC for every DS, using a separate kernel process
	 * for every DS except the last one.
	 */
	error = 0;
	for (i = 0; i < mirrorcnt - 1; i++, tdrpc++) {
		tdrpc->done = 0;
		NFSBCOPY(fhp, &tdrpc->fh, sizeof(*fhp));
		tdrpc->off = off;
		tdrpc->len = len;
		tdrpc->nmp = *nmpp;
		tdrpc->cred = cred;
		tdrpc->p = p;
		tdrpc->inprog = 0;
		tdrpc->err = 0;
		tdrpc->m = m_copym(*mpp, offs, NFSM_RNDUP(len), M_WAITOK);
		ret = EIO;
		if (nfs_pnfsiothreads != 0) {
			ret = nfs_pnfsio(start_writedsdorpc, tdrpc);
			NFSD_DEBUG(4, "nfsrv_writedsrpc: nfs_pnfsio=%d\n",
			    ret);
		}
		if (ret != 0) {
			ret = nfsrv_writedsdorpc(*nmpp, fhp, off, len, NULL,
			    tdrpc->m, cred, p);
			if (nfsds_failerr(ret) && *failposp == -1)
				*failposp = i;
			else if (error == 0 && ret != 0)
				error = ret;
		}
		nmpp++;
		fhp++;
	}
	m = m_copym(*mpp, offs, NFSM_RNDUP(len), M_WAITOK);
	ret = nfsrv_writedsdorpc(*nmpp, fhp, off, len, &na, m, cred, p);
	if (nfsds_failerr(ret) && *failposp == -1 && mirrorcnt > 1)
		*failposp = mirrorcnt - 1;
	else if (error == 0 && ret != 0)
		error = ret;
	if (error == 0)
		error = nfsrv_setextattr(vp, &na, p);
	NFSD_DEBUG(4, "nfsrv_writedsrpc: aft setextat=%d\n", error);
	tdrpc = drpc;
	timo = hz / 50;		/* Wait for 20msec. */
	if (timo < 1)
		timo = 1;
	for (i = 0; i < mirrorcnt - 1; i++, tdrpc++) {
		/* Wait for RPCs on separate threads to complete. */
		while (tdrpc->inprog != 0 && tdrpc->done == 0)
			tsleep(&tdrpc->tsk, PVFS, "srvwrds", timo);
		if (nfsds_failerr(tdrpc->err) && *failposp == -1)
			*failposp = i;
		else if (error == 0 && tdrpc->err != 0)
			error = tdrpc->err;
	}
	free(drpc, M_TEMP);
	return (error);
}

static int
nfsrv_setattrdsdorpc(fhandle_t *fhp, struct ucred *cred, NFSPROC_T *p,
    struct vnode *vp, struct nfsmount *nmp, struct nfsvattr *nap,
    struct nfsvattr *dsnap)
{
	uint32_t *tl;
	struct nfsrv_descript *nd;
	nfsv4stateid_t st;
	nfsattrbit_t attrbits;
	int error;

	NFSD_DEBUG(4, "in nfsrv_setattrdsdorpc\n");
	nd = malloc(sizeof(*nd), M_TEMP, M_WAITOK | M_ZERO);
	/*
	 * Use a stateid where other is an alternating 01010 pattern and
	 * seqid is 0xffffffff.  This value is not defined as special by
	 * the RFC and is used by the FreeBSD NFS server to indicate an
	 * MDS->DS proxy operation.
	 */
	st.other[0] = 0x55555555;
	st.other[1] = 0x55555555;
	st.other[2] = 0x55555555;
	st.seqid = 0xffffffff;
	nfscl_reqstart(nd, NFSPROC_SETATTR, nmp, (u_int8_t *)fhp, sizeof(*fhp),
	    NULL, NULL, 0, 0);
	nfsm_stateidtom(nd, &st, NFSSTATEID_PUTSTATEID);
	nfscl_fillsattr(nd, &nap->na_vattr, vp, NFSSATTR_FULL, 0);

	/* Do a Getattr for Size, Change, Access Time and Modify Time. */
	NFSZERO_ATTRBIT(&attrbits);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_SIZE);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_CHANGE);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_TIMEACCESS);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_TIMEMODIFY);
	NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
	*tl = txdr_unsigned(NFSV4OP_GETATTR);
	(void) nfsrv_putattrbit(nd, &attrbits);
	error = newnfs_request(nd, nmp, NULL, &nmp->nm_sockreq, NULL, p, cred,
	    NFS_PROG, NFS_VER4, NULL, 1, NULL, NULL);
	if (error != 0) {
		free(nd, M_TEMP);
		return (error);
	}
	NFSD_DEBUG(4, "nfsrv_setattrdsdorpc: aft setattrrpc=%d\n",
	    nd->nd_repstat);
	/* Get rid of weak cache consistency data for now. */
	if ((nd->nd_flag & (ND_NOMOREDATA | ND_NFSV4 | ND_V4WCCATTR)) ==
	    (ND_NFSV4 | ND_V4WCCATTR)) {
		error = nfsv4_loadattr(nd, NULL, dsnap, NULL, NULL, 0, NULL,
		    NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL);
		NFSD_DEBUG(4, "nfsrv_setattrdsdorpc: wcc attr=%d\n", error);
		if (error != 0)
			goto nfsmout;
		/*
		 * Get rid of Op# and status for next op.
		 */
		NFSM_DISSECT(tl, uint32_t *, 2 * NFSX_UNSIGNED);
		if (*++tl != 0)
			nd->nd_flag |= ND_NOMOREDATA;
	}
	error = nfsrv_getattrbits(nd, &attrbits, NULL, NULL);
	if (error != 0)
		goto nfsmout;
	if (nd->nd_repstat != 0)
		error = nd->nd_repstat;
	/*
	 * Get the Change, Size, Access Time and Modify Time attributes and set
	 * on the Metadata file, so its attributes will be what the file's
	 * would be if it had been written.
	 */
	if (error == 0) {
		NFSM_DISSECT(tl, uint32_t *, 2 * NFSX_UNSIGNED);
		error = nfsv4_loadattr(nd, NULL, dsnap, NULL, NULL, 0, NULL,
		    NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL);
	}
	NFSD_DEBUG(4, "nfsrv_setattrdsdorpc: aft setattr loadattr=%d\n", error);
nfsmout:
	m_freem(nd->nd_mrep);
	free(nd, M_TEMP);
	NFSD_DEBUG(4, "nfsrv_setattrdsdorpc error=%d\n", error);
	return (error);
}

struct nfsrvsetattrdsdorpc {
	int			done;
	int			inprog;
	struct task		tsk;
	fhandle_t		fh;
	struct nfsmount		*nmp;
	struct vnode		*vp;
	struct ucred		*cred;
	NFSPROC_T		*p;
	struct nfsvattr		na;
	struct nfsvattr		dsna;
	int			err;
};

/*
 * Start up the thread that will execute nfsrv_setattrdsdorpc().
 */
static void
start_setattrdsdorpc(void *arg, int pending)
{
	struct nfsrvsetattrdsdorpc *drpc;

	drpc = (struct nfsrvsetattrdsdorpc *)arg;
	drpc->err = nfsrv_setattrdsdorpc(&drpc->fh, drpc->cred, drpc->p,
	    drpc->vp, drpc->nmp, &drpc->na, &drpc->dsna);
	drpc->done = 1;
}

static int
nfsrv_setattrdsrpc(fhandle_t *fhp, struct ucred *cred, NFSPROC_T *p,
    struct vnode *vp, struct nfsmount **nmpp, int mirrorcnt,
    struct nfsvattr *nap, int *failposp)
{
	struct nfsrvsetattrdsdorpc *drpc, *tdrpc;
	struct nfsvattr na;
	int error, i, ret, timo;

	NFSD_DEBUG(4, "in nfsrv_setattrdsrpc\n");
	drpc = NULL;
	if (mirrorcnt > 1)
		tdrpc = drpc = malloc(sizeof(*drpc) * (mirrorcnt - 1), M_TEMP,
		    M_WAITOK);

	/*
	 * Do the setattr RPC for every DS, using a separate kernel process
	 * for every DS except the last one.
	 */
	error = 0;
	for (i = 0; i < mirrorcnt - 1; i++, tdrpc++) {
		tdrpc->done = 0;
		tdrpc->inprog = 0;
		NFSBCOPY(fhp, &tdrpc->fh, sizeof(*fhp));
		tdrpc->nmp = *nmpp;
		tdrpc->vp = vp;
		tdrpc->cred = cred;
		tdrpc->p = p;
		tdrpc->na = *nap;
		tdrpc->err = 0;
		ret = EIO;
		if (nfs_pnfsiothreads != 0) {
			ret = nfs_pnfsio(start_setattrdsdorpc, tdrpc);
			NFSD_DEBUG(4, "nfsrv_setattrdsrpc: nfs_pnfsio=%d\n",
			    ret);
		}
		if (ret != 0) {
			ret = nfsrv_setattrdsdorpc(fhp, cred, p, vp, *nmpp, nap,
			    &na);
			if (nfsds_failerr(ret) && *failposp == -1)
				*failposp = i;
			else if (error == 0 && ret != 0)
				error = ret;
		}
		nmpp++;
		fhp++;
	}
	ret = nfsrv_setattrdsdorpc(fhp, cred, p, vp, *nmpp, nap, &na);
	if (nfsds_failerr(ret) && *failposp == -1 && mirrorcnt > 1)
		*failposp = mirrorcnt - 1;
	else if (error == 0 && ret != 0)
		error = ret;
	if (error == 0)
		error = nfsrv_setextattr(vp, &na, p);
	NFSD_DEBUG(4, "nfsrv_setattrdsrpc: aft setextat=%d\n", error);
	tdrpc = drpc;
	timo = hz / 50;		/* Wait for 20msec. */
	if (timo < 1)
		timo = 1;
	for (i = 0; i < mirrorcnt - 1; i++, tdrpc++) {
		/* Wait for RPCs on separate threads to complete. */
		while (tdrpc->inprog != 0 && tdrpc->done == 0)
			tsleep(&tdrpc->tsk, PVFS, "srvsads", timo);
		if (nfsds_failerr(tdrpc->err) && *failposp == -1)
			*failposp = i;
		else if (error == 0 && tdrpc->err != 0)
			error = tdrpc->err;
	}
	free(drpc, M_TEMP);
	return (error);
}

/*
 * Do a Setattr of an NFSv4 ACL on the DS file.
 */
static int
nfsrv_setacldsdorpc(fhandle_t *fhp, struct ucred *cred, NFSPROC_T *p,
    struct vnode *vp, struct nfsmount *nmp, struct acl *aclp)
{
	struct nfsrv_descript *nd;
	nfsv4stateid_t st;
	nfsattrbit_t attrbits;
	int error;

	NFSD_DEBUG(4, "in nfsrv_setacldsdorpc\n");
	nd = malloc(sizeof(*nd), M_TEMP, M_WAITOK | M_ZERO);
	/*
	 * Use a stateid where other is an alternating 01010 pattern and
	 * seqid is 0xffffffff.  This value is not defined as special by
	 * the RFC and is used by the FreeBSD NFS server to indicate an
	 * MDS->DS proxy operation.
	 */
	st.other[0] = 0x55555555;
	st.other[1] = 0x55555555;
	st.other[2] = 0x55555555;
	st.seqid = 0xffffffff;
	nfscl_reqstart(nd, NFSPROC_SETACL, nmp, (u_int8_t *)fhp, sizeof(*fhp),
	    NULL, NULL, 0, 0);
	nfsm_stateidtom(nd, &st, NFSSTATEID_PUTSTATEID);
	NFSZERO_ATTRBIT(&attrbits);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_ACL);
	/*
	 * The "vp" argument to nfsv4_fillattr() is only used for vnode_type(),
	 * so passing in the metadata "vp" will be ok, since it is of
	 * the same type (VREG).
	 */
	nfsv4_fillattr(nd, NULL, vp, aclp, NULL, NULL, 0, &attrbits, NULL,
	    NULL, 0, 0, 0, 0, 0, NULL);
	error = newnfs_request(nd, nmp, NULL, &nmp->nm_sockreq, NULL, p, cred,
	    NFS_PROG, NFS_VER4, NULL, 1, NULL, NULL);
	if (error != 0) {
		free(nd, M_TEMP);
		return (error);
	}
	NFSD_DEBUG(4, "nfsrv_setacldsdorpc: aft setaclrpc=%d\n",
	    nd->nd_repstat);
	error = nd->nd_repstat;
	m_freem(nd->nd_mrep);
	free(nd, M_TEMP);
	return (error);
}

struct nfsrvsetacldsdorpc {
	int			done;
	int			inprog;
	struct task		tsk;
	fhandle_t		fh;
	struct nfsmount		*nmp;
	struct vnode		*vp;
	struct ucred		*cred;
	NFSPROC_T		*p;
	struct acl		*aclp;
	int			err;
};

/*
 * Start up the thread that will execute nfsrv_setacldsdorpc().
 */
static void
start_setacldsdorpc(void *arg, int pending)
{
	struct nfsrvsetacldsdorpc *drpc;

	drpc = (struct nfsrvsetacldsdorpc *)arg;
	drpc->err = nfsrv_setacldsdorpc(&drpc->fh, drpc->cred, drpc->p,
	    drpc->vp, drpc->nmp, drpc->aclp);
	drpc->done = 1;
}

static int
nfsrv_setacldsrpc(fhandle_t *fhp, struct ucred *cred, NFSPROC_T *p,
    struct vnode *vp, struct nfsmount **nmpp, int mirrorcnt, struct acl *aclp,
    int *failposp)
{
	struct nfsrvsetacldsdorpc *drpc, *tdrpc;
	int error, i, ret, timo;

	NFSD_DEBUG(4, "in nfsrv_setacldsrpc\n");
	drpc = NULL;
	if (mirrorcnt > 1)
		tdrpc = drpc = malloc(sizeof(*drpc) * (mirrorcnt - 1), M_TEMP,
		    M_WAITOK);

	/*
	 * Do the setattr RPC for every DS, using a separate kernel process
	 * for every DS except the last one.
	 */
	error = 0;
	for (i = 0; i < mirrorcnt - 1; i++, tdrpc++) {
		tdrpc->done = 0;
		tdrpc->inprog = 0;
		NFSBCOPY(fhp, &tdrpc->fh, sizeof(*fhp));
		tdrpc->nmp = *nmpp;
		tdrpc->vp = vp;
		tdrpc->cred = cred;
		tdrpc->p = p;
		tdrpc->aclp = aclp;
		tdrpc->err = 0;
		ret = EIO;
		if (nfs_pnfsiothreads != 0) {
			ret = nfs_pnfsio(start_setacldsdorpc, tdrpc);
			NFSD_DEBUG(4, "nfsrv_setacldsrpc: nfs_pnfsio=%d\n",
			    ret);
		}
		if (ret != 0) {
			ret = nfsrv_setacldsdorpc(fhp, cred, p, vp, *nmpp,
			    aclp);
			if (nfsds_failerr(ret) && *failposp == -1)
				*failposp = i;
			else if (error == 0 && ret != 0)
				error = ret;
		}
		nmpp++;
		fhp++;
	}
	ret = nfsrv_setacldsdorpc(fhp, cred, p, vp, *nmpp, aclp);
	if (nfsds_failerr(ret) && *failposp == -1 && mirrorcnt > 1)
		*failposp = mirrorcnt - 1;
	else if (error == 0 && ret != 0)
		error = ret;
	NFSD_DEBUG(4, "nfsrv_setacldsrpc: aft setextat=%d\n", error);
	tdrpc = drpc;
	timo = hz / 50;		/* Wait for 20msec. */
	if (timo < 1)
		timo = 1;
	for (i = 0; i < mirrorcnt - 1; i++, tdrpc++) {
		/* Wait for RPCs on separate threads to complete. */
		while (tdrpc->inprog != 0 && tdrpc->done == 0)
			tsleep(&tdrpc->tsk, PVFS, "srvacds", timo);
		if (nfsds_failerr(tdrpc->err) && *failposp == -1)
			*failposp = i;
		else if (error == 0 && tdrpc->err != 0)
			error = tdrpc->err;
	}
	free(drpc, M_TEMP);
	return (error);
}

/*
 * Getattr call to the DS for the Modify, Size and Change attributes.
 */
static int
nfsrv_getattrdsrpc(fhandle_t *fhp, struct ucred *cred, NFSPROC_T *p,
    struct vnode *vp, struct nfsmount *nmp, struct nfsvattr *nap)
{
	struct nfsrv_descript *nd;
	int error;
	nfsattrbit_t attrbits;
	
	NFSD_DEBUG(4, "in nfsrv_getattrdsrpc\n");
	nd = malloc(sizeof(*nd), M_TEMP, M_WAITOK | M_ZERO);
	nfscl_reqstart(nd, NFSPROC_GETATTR, nmp, (u_int8_t *)fhp,
	    sizeof(fhandle_t), NULL, NULL, 0, 0);
	NFSZERO_ATTRBIT(&attrbits);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_SIZE);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_CHANGE);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_TIMEACCESS);
	NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_TIMEMODIFY);
	(void) nfsrv_putattrbit(nd, &attrbits);
	error = newnfs_request(nd, nmp, NULL, &nmp->nm_sockreq, NULL, p, cred,
	    NFS_PROG, NFS_VER4, NULL, 1, NULL, NULL);
	if (error != 0) {
		free(nd, M_TEMP);
		return (error);
	}
	NFSD_DEBUG(4, "nfsrv_getattrdsrpc: aft getattrrpc=%d\n",
	    nd->nd_repstat);
	if (nd->nd_repstat == 0) {
		error = nfsv4_loadattr(nd, NULL, nap, NULL, NULL, 0,
		    NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL,
		    NULL, NULL);
		/*
		 * We can only save the updated values in the extended
		 * attribute if the vp is exclusively locked.
		 * This should happen when any of the following operations
		 * occur on the vnode:
		 *    Close, Delegreturn, LayoutCommit, LayoutReturn
		 * As such, the updated extended attribute should get saved
		 * before nfsrv_checkdsattr() returns 0 and allows the cached
		 * attributes to be returned without calling this function.
		 */
		if (error == 0 && VOP_ISLOCKED(vp) == LK_EXCLUSIVE) {
			error = nfsrv_setextattr(vp, nap, p);
			NFSD_DEBUG(4, "nfsrv_getattrdsrpc: aft setextat=%d\n",
			    error);
		}
	} else
		error = nd->nd_repstat;
	m_freem(nd->nd_mrep);
	free(nd, M_TEMP);
	NFSD_DEBUG(4, "nfsrv_getattrdsrpc error=%d\n", error);
	return (error);
}

/*
 * Get the device id and file handle for a DS file.
 */
int
nfsrv_dsgetdevandfh(struct vnode *vp, NFSPROC_T *p, int *mirrorcntp,
    fhandle_t *fhp, char *devid)
{
	int buflen, error;
	char *buf;

	buflen = 1024;
	buf = malloc(buflen, M_TEMP, M_WAITOK);
	error = nfsrv_dsgetsockmnt(vp, 0, buf, &buflen, mirrorcntp, p, NULL,
	    fhp, devid, NULL, NULL, NULL, NULL, NULL, NULL);
	free(buf, M_TEMP);
	return (error);
}

/*
 * Do a Lookup against the DS for the filename.
 */
static int
nfsrv_pnfslookupds(struct vnode *vp, struct vnode *dvp, struct pnfsdsfile *pf,
    struct vnode **nvpp, NFSPROC_T *p)
{
	struct nameidata named;
	struct ucred *tcred;
	char *bufp;
	u_long *hashp;
	struct vnode *nvp;
	int error;

	tcred = newnfs_getcred();
	named.ni_cnd.cn_nameiop = LOOKUP;
	named.ni_cnd.cn_lkflags = LK_SHARED | LK_RETRY;
	named.ni_cnd.cn_cred = tcred;
	named.ni_cnd.cn_thread = p;
	named.ni_cnd.cn_flags = ISLASTCN | LOCKPARENT | LOCKLEAF | SAVENAME;
	nfsvno_setpathbuf(&named, &bufp, &hashp);
	named.ni_cnd.cn_nameptr = bufp;
	named.ni_cnd.cn_namelen = strlen(pf->dsf_filename);
	strlcpy(bufp, pf->dsf_filename, NAME_MAX);
	NFSD_DEBUG(4, "nfsrv_pnfslookupds: filename=%s\n", bufp);
	error = VOP_LOOKUP(dvp, &nvp, &named.ni_cnd);
	NFSD_DEBUG(4, "nfsrv_pnfslookupds: aft LOOKUP=%d\n", error);
	NFSFREECRED(tcred);
	nfsvno_relpathbuf(&named);
	if (error == 0)
		*nvpp = nvp;
	NFSD_DEBUG(4, "eo nfsrv_pnfslookupds=%d\n", error);
	return (error);
}

/*
 * Set the file handle to the correct one.
 */
static void
nfsrv_pnfssetfh(struct vnode *vp, struct pnfsdsfile *pf, char *devid,
    char *fnamep, struct vnode *nvp, NFSPROC_T *p)
{
	struct nfsnode *np;
	int ret;

	np = VTONFS(nvp);
	NFSBCOPY(np->n_fhp->nfh_fh, &pf->dsf_fh, NFSX_MYFH);
	/*
	 * We can only do a vn_set_extattr() if the vnode is exclusively
	 * locked and vn_start_write() has been done.  If devid != NULL or
	 * fnamep != NULL or the vnode is shared locked, vn_start_write()
	 * may not have been done.
	 * If not done now, it will be done on a future call.
	 */
	if (devid == NULL && fnamep == NULL && NFSVOPISLOCKED(vp) ==
	    LK_EXCLUSIVE)
		ret = vn_extattr_set(vp, IO_NODELOCKED,
		    EXTATTR_NAMESPACE_SYSTEM, "pnfsd.dsfile", sizeof(*pf),
		    (char *)pf, p);
	NFSD_DEBUG(4, "eo nfsrv_pnfssetfh=%d\n", ret);
}

/*
 * Cause RPCs waiting on "nmp" to fail.  This is called for a DS mount point
 * when the DS has failed.
 */
void
nfsrv_killrpcs(struct nfsmount *nmp)
{

	/*
	 * Call newnfs_nmcancelreqs() to cause
	 * any RPCs in progress on the mount point to
	 * fail.
	 * This will cause any process waiting for an
	 * RPC to complete while holding a vnode lock
	 * on the mounted-on vnode (such as "df" or
	 * a non-forced "umount") to fail.
	 * This will unlock the mounted-on vnode so
	 * a forced dismount can succeed.
	 * The NFSMNTP_CANCELRPCS flag should be set when this function is
	 * called.
	 */
	newnfs_nmcancelreqs(nmp);
}

/*
 * Sum up the statfs info for each of the DSs, so that the client will
 * receive the total for all DSs.
 */
static int
nfsrv_pnfsstatfs(struct statfs *sf, struct mount *mp)
{
	struct statfs *tsf;
	struct nfsdevice *ds;
	struct vnode **dvpp, **tdvpp, *dvp;
	uint64_t tot;
	int cnt, error = 0, i;

	if (nfsrv_devidcnt <= 0)
		return (ENXIO);
	dvpp = mallocarray(nfsrv_devidcnt, sizeof(*dvpp), M_TEMP, M_WAITOK);
	tsf = malloc(sizeof(*tsf), M_TEMP, M_WAITOK);

	/* Get an array of the dvps for the DSs. */
	tdvpp = dvpp;
	i = 0;
	NFSDDSLOCK();
	/* First, search for matches for same file system. */
	TAILQ_FOREACH(ds, &nfsrv_devidhead, nfsdev_list) {
		if (ds->nfsdev_nmp != NULL && ds->nfsdev_mdsisset != 0 &&
		    ds->nfsdev_mdsfsid.val[0] == mp->mnt_stat.f_fsid.val[0] &&
		    ds->nfsdev_mdsfsid.val[1] == mp->mnt_stat.f_fsid.val[1]) {
			if (++i > nfsrv_devidcnt)
				break;
			*tdvpp++ = ds->nfsdev_dvp;
		}
	}
	/*
	 * If no matches for same file system, total all servers not assigned
	 * to a file system.
	 */
	if (i == 0) {
		TAILQ_FOREACH(ds, &nfsrv_devidhead, nfsdev_list) {
			if (ds->nfsdev_nmp != NULL &&
			    ds->nfsdev_mdsisset == 0) {
				if (++i > nfsrv_devidcnt)
					break;
				*tdvpp++ = ds->nfsdev_dvp;
			}
		}
	}
	NFSDDSUNLOCK();
	cnt = i;

	/* Do a VFS_STATFS() for each of the DSs and sum them up. */
	tdvpp = dvpp;
	for (i = 0; i < cnt && error == 0; i++) {
		dvp = *tdvpp++;
		error = VFS_STATFS(dvp->v_mount, tsf);
		if (error == 0) {
			if (sf->f_bsize == 0) {
				if (tsf->f_bsize > 0)
					sf->f_bsize = tsf->f_bsize;
				else
					sf->f_bsize = 8192;
			}
			if (tsf->f_blocks > 0) {
				if (sf->f_bsize != tsf->f_bsize) {
					tot = tsf->f_blocks * tsf->f_bsize;
					sf->f_blocks += (tot / sf->f_bsize);
				} else
					sf->f_blocks += tsf->f_blocks;
			}
			if (tsf->f_bfree > 0) {
				if (sf->f_bsize != tsf->f_bsize) {
					tot = tsf->f_bfree * tsf->f_bsize;
					sf->f_bfree += (tot / sf->f_bsize);
				} else
					sf->f_bfree += tsf->f_bfree;
			}
			if (tsf->f_bavail > 0) {
				if (sf->f_bsize != tsf->f_bsize) {
					tot = tsf->f_bavail * tsf->f_bsize;
					sf->f_bavail += (tot / sf->f_bsize);
				} else
					sf->f_bavail += tsf->f_bavail;
			}
		}
	}
	free(tsf, M_TEMP);
	free(dvpp, M_TEMP);
	return (error);
}

/*
 * Set an NFSv4 acl.
 */
int
nfsrv_setacl(struct vnode *vp, NFSACL_T *aclp, struct ucred *cred, NFSPROC_T *p)
{
	int error;

	if (nfsrv_useacl == 0 || nfs_supportsnfsv4acls(vp) == 0) {
		error = NFSERR_ATTRNOTSUPP;
		goto out;
	}
	/*
	 * With NFSv4 ACLs, chmod(2) may need to add additional entries.
	 * Make sure it has enough room for that - splitting every entry
	 * into two and appending "canonical six" entries at the end.
	 * Cribbed out of kern/vfs_acl.c - Rick M.
	 */
	if (aclp->acl_cnt > (ACL_MAX_ENTRIES - 6) / 2) {
		error = NFSERR_ATTRNOTSUPP;
		goto out;
	}
	error = VOP_SETACL(vp, ACL_TYPE_NFS4, aclp, cred, p);
	if (error == 0) {
		error = nfsrv_dssetacl(vp, aclp, cred, p);
		if (error == ENOENT)
			error = 0;
	}

out:
	NFSEXITCODE(error);
	return (error);
}

extern int (*nfsd_call_nfsd)(struct thread *, struct nfssvc_args *);

/*
 * Called once to initialize data structures...
 */
static int
nfsd_modevent(module_t mod, int type, void *data)
{
	int error = 0, i;
	static int loaded = 0;

	switch (type) {
	case MOD_LOAD:
		if (loaded)
			goto out;
		newnfs_portinit();
		for (i = 0; i < NFSRVCACHE_HASHSIZE; i++) {
			mtx_init(&nfsrchash_table[i].mtx, "nfsrtc", NULL,
			    MTX_DEF);
			mtx_init(&nfsrcahash_table[i].mtx, "nfsrtca", NULL,
			    MTX_DEF);
		}
		mtx_init(&nfsrc_udpmtx, "nfsuc", NULL, MTX_DEF);
		mtx_init(&nfs_v4root_mutex, "nfs4rt", NULL, MTX_DEF);
		mtx_init(&nfsv4root_mnt.mnt_mtx, "nfs4mnt", NULL, MTX_DEF);
		mtx_init(&nfsrv_dontlistlock_mtx, "nfs4dnl", NULL, MTX_DEF);
		mtx_init(&nfsrv_recalllock_mtx, "nfs4rec", NULL, MTX_DEF);
		lockinit(&nfsv4root_mnt.mnt_explock, PVFS, "explock", 0, 0);
		nfsrvd_initcache();
		nfsd_init();
		NFSD_LOCK();
		nfsrvd_init(0);
		NFSD_UNLOCK();
		nfsd_mntinit();
#ifdef VV_DISABLEDELEG
		vn_deleg_ops.vndeleg_recall = nfsd_recalldelegation;
		vn_deleg_ops.vndeleg_disable = nfsd_disabledelegation;
#endif
		nfsd_call_servertimer = nfsrv_servertimer;
		nfsd_call_nfsd = nfssvc_nfsd;
		loaded = 1;
		break;

	case MOD_UNLOAD:
		if (newnfs_numnfsd != 0) {
			error = EBUSY;
			break;
		}

#ifdef VV_DISABLEDELEG
		vn_deleg_ops.vndeleg_recall = NULL;
		vn_deleg_ops.vndeleg_disable = NULL;
#endif
		nfsd_call_servertimer = NULL;
		nfsd_call_nfsd = NULL;

		/* Clean out all NFSv4 state. */
		nfsrv_throwawayallstate(curthread);

		/* Clean the NFS server reply cache */
		nfsrvd_cleancache();

		/* Free up the krpc server pool. */
		if (nfsrvd_pool != NULL)
			svcpool_destroy(nfsrvd_pool);

		/* and get rid of the locks */
		for (i = 0; i < NFSRVCACHE_HASHSIZE; i++) {
			mtx_destroy(&nfsrchash_table[i].mtx);
			mtx_destroy(&nfsrcahash_table[i].mtx);
		}
		mtx_destroy(&nfsrc_udpmtx);
		mtx_destroy(&nfs_v4root_mutex);
		mtx_destroy(&nfsv4root_mnt.mnt_mtx);
		mtx_destroy(&nfsrv_dontlistlock_mtx);
		mtx_destroy(&nfsrv_recalllock_mtx);
		for (i = 0; i < nfsrv_sessionhashsize; i++)
			mtx_destroy(&nfssessionhash[i].mtx);
		if (nfslayouthash != NULL) {
			for (i = 0; i < nfsrv_layouthashsize; i++)
				mtx_destroy(&nfslayouthash[i].mtx);
			free(nfslayouthash, M_NFSDSESSION);
		}
		lockdestroy(&nfsv4root_mnt.mnt_explock);
		free(nfsclienthash, M_NFSDCLIENT);
		free(nfslockhash, M_NFSDLOCKFILE);
		free(nfssessionhash, M_NFSDSESSION);
		loaded = 0;
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

out:
	NFSEXITCODE(error);
	return (error);
}
static moduledata_t nfsd_mod = {
	"nfsd",
	nfsd_modevent,
	NULL,
};
DECLARE_MODULE(nfsd, nfsd_mod, SI_SUB_VFS, SI_ORDER_ANY);

/* So that loader and kldload(2) can find us, wherever we are.. */
MODULE_VERSION(nfsd, 1);
MODULE_DEPEND(nfsd, nfscommon, 1, 1, 1);
MODULE_DEPEND(nfsd, nfslock, 1, 1, 1);
MODULE_DEPEND(nfsd, nfslockd, 1, 1, 1);
MODULE_DEPEND(nfsd, krpc, 1, 1, 1);
MODULE_DEPEND(nfsd, nfssvc, 1, 1, 1);

