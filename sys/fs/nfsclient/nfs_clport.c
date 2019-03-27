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

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/capsicum.h>

/*
 * generally, I don't like #includes inside .h files, but it seems to
 * be the easiest way to handle the port.
 */
#include <sys/fail.h>
#include <sys/hash.h>
#include <sys/sysctl.h>
#include <fs/nfs/nfsport.h>
#include <netinet/in_fib.h>
#include <netinet/if_ether.h>
#include <netinet6/ip6_var.h>
#include <net/if_types.h>

#include <fs/nfsclient/nfs_kdtrace.h>

#ifdef KDTRACE_HOOKS
dtrace_nfsclient_attrcache_flush_probe_func_t
		dtrace_nfscl_attrcache_flush_done_probe;
uint32_t	nfscl_attrcache_flush_done_id;

dtrace_nfsclient_attrcache_get_hit_probe_func_t
		dtrace_nfscl_attrcache_get_hit_probe;
uint32_t	nfscl_attrcache_get_hit_id;

dtrace_nfsclient_attrcache_get_miss_probe_func_t
		dtrace_nfscl_attrcache_get_miss_probe;
uint32_t	nfscl_attrcache_get_miss_id;

dtrace_nfsclient_attrcache_load_probe_func_t
		dtrace_nfscl_attrcache_load_done_probe;
uint32_t	nfscl_attrcache_load_done_id;
#endif /* !KDTRACE_HOOKS */

extern u_int32_t newnfs_true, newnfs_false, newnfs_xdrneg1;
extern struct vop_vector newnfs_vnodeops;
extern struct vop_vector newnfs_fifoops;
extern uma_zone_t newnfsnode_zone;
extern struct buf_ops buf_ops_newnfs;
extern uma_zone_t ncl_pbuf_zone;
extern short nfsv4_cbport;
extern int nfscl_enablecallb;
extern int nfs_numnfscbd;
extern int nfscl_inited;
struct mtx ncl_iod_mutex;
NFSDLOCKMUTEX;
extern struct mtx nfsrv_dslock_mtx;

extern void (*ncl_call_invalcaches)(struct vnode *);

SYSCTL_DECL(_vfs_nfs);
static int ncl_fileid_maxwarnings = 10;
SYSCTL_INT(_vfs_nfs, OID_AUTO, fileid_maxwarnings, CTLFLAG_RWTUN,
    &ncl_fileid_maxwarnings, 0,
    "Limit fileid corruption warnings; 0 is off; -1 is unlimited");
static volatile int ncl_fileid_nwarnings;

static void nfscl_warn_fileid(struct nfsmount *, struct nfsvattr *,
    struct nfsvattr *);

/*
 * Comparison function for vfs_hash functions.
 */
int
newnfs_vncmpf(struct vnode *vp, void *arg)
{
	struct nfsfh *nfhp = (struct nfsfh *)arg;
	struct nfsnode *np = VTONFS(vp);

	if (np->n_fhp->nfh_len != nfhp->nfh_len ||
	    NFSBCMP(np->n_fhp->nfh_fh, nfhp->nfh_fh, nfhp->nfh_len))
		return (1);
	return (0);
}

/*
 * Look up a vnode/nfsnode by file handle.
 * Callers must check for mount points!!
 * In all cases, a pointer to a
 * nfsnode structure is returned.
 * This variant takes a "struct nfsfh *" as second argument and uses
 * that structure up, either by hanging off the nfsnode or FREEing it.
 */
int
nfscl_nget(struct mount *mntp, struct vnode *dvp, struct nfsfh *nfhp,
    struct componentname *cnp, struct thread *td, struct nfsnode **npp,
    void *stuff, int lkflags)
{
	struct nfsnode *np, *dnp;
	struct vnode *vp, *nvp;
	struct nfsv4node *newd, *oldd;
	int error;
	u_int hash;
	struct nfsmount *nmp;

	nmp = VFSTONFS(mntp);
	dnp = VTONFS(dvp);
	*npp = NULL;

	hash = fnv_32_buf(nfhp->nfh_fh, nfhp->nfh_len, FNV1_32_INIT);

	error = vfs_hash_get(mntp, hash, lkflags,
	    td, &nvp, newnfs_vncmpf, nfhp);
	if (error == 0 && nvp != NULL) {
		/*
		 * I believe there is a slight chance that vgonel() could
		 * get called on this vnode between when NFSVOPLOCK() drops
		 * the VI_LOCK() and vget() acquires it again, so that it
		 * hasn't yet had v_usecount incremented. If this were to
		 * happen, the VI_DOOMED flag would be set, so check for
		 * that here. Since we now have the v_usecount incremented,
		 * we should be ok until we vrele() it, if the VI_DOOMED
		 * flag isn't set now.
		 */
		VI_LOCK(nvp);
		if ((nvp->v_iflag & VI_DOOMED)) {
			VI_UNLOCK(nvp);
			vrele(nvp);
			error = ENOENT;
		} else {
			VI_UNLOCK(nvp);
		}
	}
	if (error) {
		free(nfhp, M_NFSFH);
		return (error);
	}
	if (nvp != NULL) {
		np = VTONFS(nvp);
		/*
		 * For NFSv4, check to see if it is the same name and
		 * replace the name, if it is different.
		 */
		oldd = newd = NULL;
		if ((nmp->nm_flag & NFSMNT_NFSV4) && np->n_v4 != NULL &&
		    nvp->v_type == VREG &&
		    (np->n_v4->n4_namelen != cnp->cn_namelen ||
		     NFSBCMP(cnp->cn_nameptr, NFS4NODENAME(np->n_v4),
		     cnp->cn_namelen) ||
		     dnp->n_fhp->nfh_len != np->n_v4->n4_fhlen ||
		     NFSBCMP(dnp->n_fhp->nfh_fh, np->n_v4->n4_data,
		     dnp->n_fhp->nfh_len))) {
		    newd = malloc(
			sizeof (struct nfsv4node) + dnp->n_fhp->nfh_len +
			+ cnp->cn_namelen - 1, M_NFSV4NODE, M_WAITOK);
		    NFSLOCKNODE(np);
		    if (newd != NULL && np->n_v4 != NULL && nvp->v_type == VREG
			&& (np->n_v4->n4_namelen != cnp->cn_namelen ||
			 NFSBCMP(cnp->cn_nameptr, NFS4NODENAME(np->n_v4),
			 cnp->cn_namelen) ||
			 dnp->n_fhp->nfh_len != np->n_v4->n4_fhlen ||
			 NFSBCMP(dnp->n_fhp->nfh_fh, np->n_v4->n4_data,
			 dnp->n_fhp->nfh_len))) {
			oldd = np->n_v4;
			np->n_v4 = newd;
			newd = NULL;
			np->n_v4->n4_fhlen = dnp->n_fhp->nfh_len;
			np->n_v4->n4_namelen = cnp->cn_namelen;
			NFSBCOPY(dnp->n_fhp->nfh_fh, np->n_v4->n4_data,
			    dnp->n_fhp->nfh_len);
			NFSBCOPY(cnp->cn_nameptr, NFS4NODENAME(np->n_v4),
			    cnp->cn_namelen);
		    }
		    NFSUNLOCKNODE(np);
		}
		if (newd != NULL)
			free(newd, M_NFSV4NODE);
		if (oldd != NULL)
			free(oldd, M_NFSV4NODE);
		*npp = np;
		free(nfhp, M_NFSFH);
		return (0);
	}
	np = uma_zalloc(newnfsnode_zone, M_WAITOK | M_ZERO);

	error = getnewvnode(nfs_vnode_tag, mntp, &newnfs_vnodeops, &nvp);
	if (error) {
		uma_zfree(newnfsnode_zone, np);
		free(nfhp, M_NFSFH);
		return (error);
	}
	vp = nvp;
	KASSERT(vp->v_bufobj.bo_bsize != 0, ("nfscl_nget: bo_bsize == 0"));
	vp->v_bufobj.bo_ops = &buf_ops_newnfs;
	vp->v_data = np;
	np->n_vnode = vp;
	/* 
	 * Initialize the mutex even if the vnode is going to be a loser.
	 * This simplifies the logic in reclaim, which can then unconditionally
	 * destroy the mutex (in the case of the loser, or if hash_insert
	 * happened to return an error no special casing is needed).
	 */
	mtx_init(&np->n_mtx, "NEWNFSnode lock", NULL, MTX_DEF | MTX_DUPOK);
	lockinit(&np->n_excl, PVFS, "nfsupg", VLKTIMEOUT, LK_NOSHARE |
	    LK_CANRECURSE);

	/* 
	 * Are we getting the root? If so, make sure the vnode flags
	 * are correct 
	 */
	if ((nfhp->nfh_len == nmp->nm_fhsize) &&
	    !bcmp(nfhp->nfh_fh, nmp->nm_fh, nfhp->nfh_len)) {
		if (vp->v_type == VNON)
			vp->v_type = VDIR;
		vp->v_vflag |= VV_ROOT;
	}
	
	np->n_fhp = nfhp;
	/*
	 * For NFSv4, we have to attach the directory file handle and
	 * file name, so that Open Ops can be done later.
	 */
	if (nmp->nm_flag & NFSMNT_NFSV4) {
		np->n_v4 = malloc(sizeof (struct nfsv4node)
		    + dnp->n_fhp->nfh_len + cnp->cn_namelen - 1, M_NFSV4NODE,
		    M_WAITOK);
		np->n_v4->n4_fhlen = dnp->n_fhp->nfh_len;
		np->n_v4->n4_namelen = cnp->cn_namelen;
		NFSBCOPY(dnp->n_fhp->nfh_fh, np->n_v4->n4_data,
		    dnp->n_fhp->nfh_len);
		NFSBCOPY(cnp->cn_nameptr, NFS4NODENAME(np->n_v4),
		    cnp->cn_namelen);
	} else {
		np->n_v4 = NULL;
	}

	/*
	 * NFS supports recursive and shared locking.
	 */
	lockmgr(vp->v_vnlock, LK_EXCLUSIVE | LK_NOWITNESS, NULL);
	VN_LOCK_AREC(vp);
	VN_LOCK_ASHARE(vp);
	error = insmntque(vp, mntp);
	if (error != 0) {
		*npp = NULL;
		mtx_destroy(&np->n_mtx);
		lockdestroy(&np->n_excl);
		free(nfhp, M_NFSFH);
		if (np->n_v4 != NULL)
			free(np->n_v4, M_NFSV4NODE);
		uma_zfree(newnfsnode_zone, np);
		return (error);
	}
	error = vfs_hash_insert(vp, hash, lkflags, 
	    td, &nvp, newnfs_vncmpf, nfhp);
	if (error)
		return (error);
	if (nvp != NULL) {
		*npp = VTONFS(nvp);
		/* vfs_hash_insert() vput()'s the losing vnode */
		return (0);
	}
	*npp = np;

	return (0);
}

/*
 * Another variant of nfs_nget(). This one is only used by reopen. It
 * takes almost the same args as nfs_nget(), but only succeeds if an entry
 * exists in the cache. (Since files should already be "open" with a
 * vnode ref cnt on the node when reopen calls this, it should always
 * succeed.)
 * Also, don't get a vnode lock, since it may already be locked by some
 * other process that is handling it. This is ok, since all other threads
 * on the client are blocked by the nfsc_lock being exclusively held by the
 * caller of this function.
 */
int
nfscl_ngetreopen(struct mount *mntp, u_int8_t *fhp, int fhsize,
    struct thread *td, struct nfsnode **npp)
{
	struct vnode *nvp;
	u_int hash;
	struct nfsfh *nfhp;
	int error;

	*npp = NULL;
	/* For forced dismounts, just return error. */
	if (NFSCL_FORCEDISM(mntp))
		return (EINTR);
	nfhp = malloc(sizeof (struct nfsfh) + fhsize,
	    M_NFSFH, M_WAITOK);
	bcopy(fhp, &nfhp->nfh_fh[0], fhsize);
	nfhp->nfh_len = fhsize;

	hash = fnv_32_buf(fhp, fhsize, FNV1_32_INIT);

	/*
	 * First, try to get the vnode locked, but don't block for the lock.
	 */
	error = vfs_hash_get(mntp, hash, (LK_EXCLUSIVE | LK_NOWAIT), td, &nvp,
	    newnfs_vncmpf, nfhp);
	if (error == 0 && nvp != NULL) {
		NFSVOPUNLOCK(nvp, 0);
	} else if (error == EBUSY) {
		/*
		 * It is safe so long as a vflush() with
		 * FORCECLOSE has not been done. Since the Renew thread is
		 * stopped and the MNTK_UNMOUNTF flag is set before doing
		 * a vflush() with FORCECLOSE, we should be ok here.
		 */
		if (NFSCL_FORCEDISM(mntp))
			error = EINTR;
		else {
			vfs_hash_ref(mntp, hash, td, &nvp, newnfs_vncmpf, nfhp);
			if (nvp == NULL) {
				error = ENOENT;
			} else if ((nvp->v_iflag & VI_DOOMED) != 0) {
				error = ENOENT;
				vrele(nvp);
			} else {
				error = 0;
			}
		}
	}
	free(nfhp, M_NFSFH);
	if (error)
		return (error);
	if (nvp != NULL) {
		*npp = VTONFS(nvp);
		return (0);
	}
	return (EINVAL);
}

static void
nfscl_warn_fileid(struct nfsmount *nmp, struct nfsvattr *oldnap,
    struct nfsvattr *newnap)
{
	int off;

	if (ncl_fileid_maxwarnings >= 0 &&
	    ncl_fileid_nwarnings >= ncl_fileid_maxwarnings)
		return;
	off = 0;
	if (ncl_fileid_maxwarnings >= 0) {
		if (++ncl_fileid_nwarnings >= ncl_fileid_maxwarnings)
			off = 1;
	}

	printf("newnfs: server '%s' error: fileid changed. "
	    "fsid %jx:%jx: expected fileid %#jx, got %#jx. "
	    "(BROKEN NFS SERVER OR MIDDLEWARE)\n",
	    nmp->nm_com.nmcom_hostname,
	    (uintmax_t)nmp->nm_fsid[0],
	    (uintmax_t)nmp->nm_fsid[1],
	    (uintmax_t)oldnap->na_fileid,
	    (uintmax_t)newnap->na_fileid);

	if (off)
		printf("newnfs: Logged %d times about fileid corruption; "
		    "going quiet to avoid spamming logs excessively. (Limit "
		    "is: %d).\n", ncl_fileid_nwarnings,
		    ncl_fileid_maxwarnings);
}

/*
 * Load the attribute cache (that lives in the nfsnode entry) with
 * the attributes of the second argument and
 * Iff vaper not NULL
 *    copy the attributes to *vaper
 * Similar to nfs_loadattrcache(), except the attributes are passed in
 * instead of being parsed out of the mbuf list.
 */
int
nfscl_loadattrcache(struct vnode **vpp, struct nfsvattr *nap, void *nvaper,
    void *stuff, int writeattr, int dontshrink)
{
	struct vnode *vp = *vpp;
	struct vattr *vap, *nvap = &nap->na_vattr, *vaper = nvaper;
	struct nfsnode *np;
	struct nfsmount *nmp;
	struct timespec mtime_save;
	u_quad_t nsize;
	int setnsize, error, force_fid_err;

	error = 0;
	setnsize = 0;
	nsize = 0;

	/*
	 * If v_type == VNON it is a new node, so fill in the v_type,
	 * n_mtime fields. Check to see if it represents a special 
	 * device, and if so, check for a possible alias. Once the
	 * correct vnode has been obtained, fill in the rest of the
	 * information.
	 */
	np = VTONFS(vp);
	NFSLOCKNODE(np);
	if (vp->v_type != nvap->va_type) {
		vp->v_type = nvap->va_type;
		if (vp->v_type == VFIFO)
			vp->v_op = &newnfs_fifoops;
		np->n_mtime = nvap->va_mtime;
	}
	nmp = VFSTONFS(vp->v_mount);
	vap = &np->n_vattr.na_vattr;
	mtime_save = vap->va_mtime;
	if (writeattr) {
		np->n_vattr.na_filerev = nap->na_filerev;
		np->n_vattr.na_size = nap->na_size;
		np->n_vattr.na_mtime = nap->na_mtime;
		np->n_vattr.na_ctime = nap->na_ctime;
		np->n_vattr.na_fsid = nap->na_fsid;
		np->n_vattr.na_mode = nap->na_mode;
	} else {
		force_fid_err = 0;
		KFAIL_POINT_ERROR(DEBUG_FP, nfscl_force_fileid_warning,
		    force_fid_err);
		/*
		 * BROKEN NFS SERVER OR MIDDLEWARE
		 *
		 * Certain NFS servers (certain old proprietary filers ca.
		 * 2006) or broken middleboxes (e.g. WAN accelerator products)
		 * will respond to GETATTR requests with results for a
		 * different fileid.
		 *
		 * The WAN accelerator we've observed not only serves stale
		 * cache results for a given file, it also occasionally serves
		 * results for wholly different files.  This causes surprising
		 * problems; for example the cached size attribute of a file
		 * may truncate down and then back up, resulting in zero
		 * regions in file contents read by applications.  We observed
		 * this reliably with Clang and .c files during parallel build.
		 * A pcap revealed packet fragmentation and GETATTR RPC
		 * responses with wholly wrong fileids.
		 */
		if ((np->n_vattr.na_fileid != 0 &&
		     np->n_vattr.na_fileid != nap->na_fileid) ||
		    force_fid_err) {
			nfscl_warn_fileid(nmp, &np->n_vattr, nap);
			error = EIDRM;
			goto out;
		}
		NFSBCOPY((caddr_t)nap, (caddr_t)&np->n_vattr,
		    sizeof (struct nfsvattr));
	}

	/*
	 * For NFSv4, if the node's fsid is not equal to the mount point's
	 * fsid, return the low order 32bits of the node's fsid. This
	 * allows getcwd(3) to work. There is a chance that the fsid might
	 * be the same as a local fs, but since this is in an NFS mount
	 * point, I don't think that will cause any problems?
	 */
	if (NFSHASNFSV4(nmp) && NFSHASHASSETFSID(nmp) &&
	    (nmp->nm_fsid[0] != np->n_vattr.na_filesid[0] ||
	     nmp->nm_fsid[1] != np->n_vattr.na_filesid[1])) {
		/*
		 * va_fsid needs to be set to some value derived from
		 * np->n_vattr.na_filesid that is not equal
		 * vp->v_mount->mnt_stat.f_fsid[0], so that it changes
		 * from the value used for the top level server volume
		 * in the mounted subtree.
		 */
		vn_fsid(vp, vap);
		if ((uint32_t)vap->va_fsid == np->n_vattr.na_filesid[0])
			vap->va_fsid = hash32_buf(
			    np->n_vattr.na_filesid, 2 * sizeof(uint64_t), 0);
	} else
		vn_fsid(vp, vap);
	np->n_attrstamp = time_second;
	if (vap->va_size != np->n_size) {
		if (vap->va_type == VREG) {
			if (dontshrink && vap->va_size < np->n_size) {
				/*
				 * We've been told not to shrink the file;
				 * zero np->n_attrstamp to indicate that
				 * the attributes are stale.
				 */
				vap->va_size = np->n_size;
				np->n_attrstamp = 0;
				KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp);
				vnode_pager_setsize(vp, np->n_size);
			} else if (np->n_flag & NMODIFIED) {
				/*
				 * We've modified the file: Use the larger
				 * of our size, and the server's size.
				 */
				if (vap->va_size < np->n_size) {
					vap->va_size = np->n_size;
				} else {
					np->n_size = vap->va_size;
					np->n_flag |= NSIZECHANGED;
				}
				vnode_pager_setsize(vp, np->n_size);
			} else if (vap->va_size < np->n_size) {
				/*
				 * When shrinking the size, the call to
				 * vnode_pager_setsize() cannot be done
				 * with the mutex held, so delay it until
				 * after the mtx_unlock call.
				 */
				nsize = np->n_size = vap->va_size;
				np->n_flag |= NSIZECHANGED;
				setnsize = 1;
			} else {
				np->n_size = vap->va_size;
				np->n_flag |= NSIZECHANGED;
				vnode_pager_setsize(vp, np->n_size);
			}
		} else {
			np->n_size = vap->va_size;
		}
	}
	/*
	 * The following checks are added to prevent a race between (say)
	 * a READDIR+ and a WRITE. 
	 * READDIR+, WRITE requests sent out.
	 * READDIR+ resp, WRITE resp received on client.
	 * However, the WRITE resp was handled before the READDIR+ resp
	 * causing the post op attrs from the write to be loaded first
	 * and the attrs from the READDIR+ to be loaded later. If this 
	 * happens, we have stale attrs loaded into the attrcache.
	 * We detect this by for the mtime moving back. We invalidate the 
	 * attrcache when this happens.
	 */
	if (timespeccmp(&mtime_save, &vap->va_mtime, >)) {
		/* Size changed or mtime went backwards */
		np->n_attrstamp = 0;
		KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp);
	}
	if (vaper != NULL) {
		NFSBCOPY((caddr_t)vap, (caddr_t)vaper, sizeof(*vap));
		if (np->n_flag & NCHG) {
			if (np->n_flag & NACC)
				vaper->va_atime = np->n_atim;
			if (np->n_flag & NUPD)
				vaper->va_mtime = np->n_mtim;
		}
	}

out:
#ifdef KDTRACE_HOOKS
	if (np->n_attrstamp != 0)
		KDTRACE_NFS_ATTRCACHE_LOAD_DONE(vp, vap, error);
#endif
	NFSUNLOCKNODE(np);
	if (setnsize)
		vnode_pager_setsize(vp, nsize);
	return (error);
}

/*
 * Fill in the client id name. For these bytes:
 * 1 - they must be unique
 * 2 - they should be persistent across client reboots
 * 1 is more critical than 2
 * Use the mount point's unique id plus either the uuid or, if that
 * isn't set, random junk.
 */
void
nfscl_fillclid(u_int64_t clval, char *uuid, u_int8_t *cp, u_int16_t idlen)
{
	int uuidlen;

	/*
	 * First, put in the 64bit mount point identifier.
	 */
	if (idlen >= sizeof (u_int64_t)) {
		NFSBCOPY((caddr_t)&clval, cp, sizeof (u_int64_t));
		cp += sizeof (u_int64_t);
		idlen -= sizeof (u_int64_t);
	}

	/*
	 * If uuid is non-zero length, use it.
	 */
	uuidlen = strlen(uuid);
	if (uuidlen > 0 && idlen >= uuidlen) {
		NFSBCOPY(uuid, cp, uuidlen);
		cp += uuidlen;
		idlen -= uuidlen;
	}

	/*
	 * This only normally happens if the uuid isn't set.
	 */
	while (idlen > 0) {
		*cp++ = (u_int8_t)(arc4random() % 256);
		idlen--;
	}
}

/*
 * Fill in a lock owner name. For now, pid + the process's creation time.
 */
void
nfscl_filllockowner(void *id, u_int8_t *cp, int flags)
{
	union {
		u_int32_t	lval;
		u_int8_t	cval[4];
	} tl;
	struct proc *p;

	if (id == NULL) {
		/* Return the single open_owner of all 0 bytes. */
		bzero(cp, NFSV4CL_LOCKNAMELEN);
		return;
	}
	if ((flags & F_POSIX) != 0) {
		p = (struct proc *)id;
		tl.lval = p->p_pid;
		*cp++ = tl.cval[0];
		*cp++ = tl.cval[1];
		*cp++ = tl.cval[2];
		*cp++ = tl.cval[3];
		tl.lval = p->p_stats->p_start.tv_sec;
		*cp++ = tl.cval[0];
		*cp++ = tl.cval[1];
		*cp++ = tl.cval[2];
		*cp++ = tl.cval[3];
		tl.lval = p->p_stats->p_start.tv_usec;
		*cp++ = tl.cval[0];
		*cp++ = tl.cval[1];
		*cp++ = tl.cval[2];
		*cp = tl.cval[3];
	} else if ((flags & F_FLOCK) != 0) {
		bcopy(&id, cp, sizeof(id));
		bzero(&cp[sizeof(id)], NFSV4CL_LOCKNAMELEN - sizeof(id));
	} else {
		printf("nfscl_filllockowner: not F_POSIX or F_FLOCK\n");
		bzero(cp, NFSV4CL_LOCKNAMELEN);
	}
}

/*
 * Find the parent process for the thread passed in as an argument.
 * If none exists, return NULL, otherwise return a thread for the parent.
 * (Can be any of the threads, since it is only used for td->td_proc.)
 */
NFSPROC_T *
nfscl_getparent(struct thread *td)
{
	struct proc *p;
	struct thread *ptd;

	if (td == NULL)
		return (NULL);
	p = td->td_proc;
	if (p->p_pid == 0)
		return (NULL);
	p = p->p_pptr;
	if (p == NULL)
		return (NULL);
	ptd = TAILQ_FIRST(&p->p_threads);
	return (ptd);
}

/*
 * Start up the renew kernel thread.
 */
static void
start_nfscl(void *arg)
{
	struct nfsclclient *clp;
	struct thread *td;

	clp = (struct nfsclclient *)arg;
	td = TAILQ_FIRST(&clp->nfsc_renewthread->p_threads);
	nfscl_renewthread(clp, td);
	kproc_exit(0);
}

void
nfscl_start_renewthread(struct nfsclclient *clp)
{

	kproc_create(start_nfscl, (void *)clp, &clp->nfsc_renewthread, 0, 0,
	    "nfscl");
}

/*
 * Handle wcc_data.
 * For NFSv4, it assumes that nfsv4_wccattr() was used to set up the getattr
 * as the first Op after PutFH.
 * (For NFSv4, the postop attributes are after the Op, so they can't be
 *  parsed here. A separate call to nfscl_postop_attr() is required.)
 */
int
nfscl_wcc_data(struct nfsrv_descript *nd, struct vnode *vp,
    struct nfsvattr *nap, int *flagp, int *wccflagp, void *stuff)
{
	u_int32_t *tl;
	struct nfsnode *np = VTONFS(vp);
	struct nfsvattr nfsva;
	int error = 0;

	if (wccflagp != NULL)
		*wccflagp = 0;
	if (nd->nd_flag & ND_NFSV3) {
		*flagp = 0;
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		if (*tl == newnfs_true) {
			NFSM_DISSECT(tl, u_int32_t *, 6 * NFSX_UNSIGNED);
			if (wccflagp != NULL) {
				mtx_lock(&np->n_mtx);
				*wccflagp = (np->n_mtime.tv_sec ==
				    fxdr_unsigned(u_int32_t, *(tl + 2)) &&
				    np->n_mtime.tv_nsec ==
				    fxdr_unsigned(u_int32_t, *(tl + 3)));
				mtx_unlock(&np->n_mtx);
			}
		}
		error = nfscl_postop_attr(nd, nap, flagp, stuff);
		if (wccflagp != NULL && *flagp == 0)
			*wccflagp = 0;
	} else if ((nd->nd_flag & (ND_NOMOREDATA | ND_NFSV4 | ND_V4WCCATTR))
	    == (ND_NFSV4 | ND_V4WCCATTR)) {
		error = nfsv4_loadattr(nd, NULL, &nfsva, NULL,
		    NULL, 0, NULL, NULL, NULL, NULL, NULL, 0,
		    NULL, NULL, NULL, NULL, NULL);
		if (error)
			return (error);
		/*
		 * Get rid of Op# and status for next op.
		 */
		NFSM_DISSECT(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
		if (*++tl)
			nd->nd_flag |= ND_NOMOREDATA;
		if (wccflagp != NULL &&
		    nfsva.na_vattr.va_mtime.tv_sec != 0) {
			mtx_lock(&np->n_mtx);
			*wccflagp = (np->n_mtime.tv_sec ==
			    nfsva.na_vattr.va_mtime.tv_sec &&
			    np->n_mtime.tv_nsec ==
			    nfsva.na_vattr.va_mtime.tv_sec);
			mtx_unlock(&np->n_mtx);
		}
	}
nfsmout:
	return (error);
}

/*
 * Get postop attributes.
 */
int
nfscl_postop_attr(struct nfsrv_descript *nd, struct nfsvattr *nap, int *retp,
    void *stuff)
{
	u_int32_t *tl;
	int error = 0;

	*retp = 0;
	if (nd->nd_flag & ND_NOMOREDATA)
		return (error);
	if (nd->nd_flag & ND_NFSV3) {
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		*retp = fxdr_unsigned(int, *tl);
	} else if (nd->nd_flag & ND_NFSV4) {
		/*
		 * For NFSv4, the postop attr are at the end, so no point
		 * in looking if nd_repstat != 0.
		 */
		if (!nd->nd_repstat) {
			NFSM_DISSECT(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			if (*(tl + 1))
				/* should never happen since nd_repstat != 0 */
				nd->nd_flag |= ND_NOMOREDATA;
			else
				*retp = 1;
		}
	} else if (!nd->nd_repstat) {
		/* For NFSv2, the attributes are here iff nd_repstat == 0 */
		*retp = 1;
	}
	if (*retp) {
		error = nfsm_loadattr(nd, nap);
		if (error)
			*retp = 0;
	}
nfsmout:
	return (error);
}

/*
 * nfscl_request() - mostly a wrapper for newnfs_request().
 */
int
nfscl_request(struct nfsrv_descript *nd, struct vnode *vp, NFSPROC_T *p,
    struct ucred *cred, void *stuff)
{
	int ret, vers;
	struct nfsmount *nmp;

	nmp = VFSTONFS(vp->v_mount);
	if (nd->nd_flag & ND_NFSV4)
		vers = NFS_VER4;
	else if (nd->nd_flag & ND_NFSV3)
		vers = NFS_VER3;
	else
		vers = NFS_VER2;
	ret = newnfs_request(nd, nmp, NULL, &nmp->nm_sockreq, vp, p, cred,
		NFS_PROG, vers, NULL, 1, NULL, NULL);
	return (ret);
}

/*
 * fill in this bsden's variant of statfs using nfsstatfs.
 */
void
nfscl_loadsbinfo(struct nfsmount *nmp, struct nfsstatfs *sfp, void *statfs)
{
	struct statfs *sbp = (struct statfs *)statfs;

	if (nmp->nm_flag & (NFSMNT_NFSV3 | NFSMNT_NFSV4)) {
		sbp->f_bsize = NFS_FABLKSIZE;
		sbp->f_blocks = sfp->sf_tbytes / NFS_FABLKSIZE;
		sbp->f_bfree = sfp->sf_fbytes / NFS_FABLKSIZE;
		/*
		 * Although sf_abytes is uint64_t and f_bavail is int64_t,
		 * the value after dividing by NFS_FABLKSIZE is small
		 * enough that it will fit in 63bits, so it is ok to
		 * assign it to f_bavail without fear that it will become
		 * negative.
		 */
		sbp->f_bavail = sfp->sf_abytes / NFS_FABLKSIZE;
		sbp->f_files = sfp->sf_tfiles;
		/* Since f_ffree is int64_t, clip it to 63bits. */
		if (sfp->sf_ffiles > INT64_MAX)
			sbp->f_ffree = INT64_MAX;
		else
			sbp->f_ffree = sfp->sf_ffiles;
	} else if ((nmp->nm_flag & NFSMNT_NFSV4) == 0) {
		/*
		 * The type casts to (int32_t) ensure that this code is
		 * compatible with the old NFS client, in that it will
		 * propagate bit31 to the high order bits. This may or may
		 * not be correct for NFSv2, but since it is a legacy
		 * environment, I'd rather retain backwards compatibility.
		 */
		sbp->f_bsize = (int32_t)sfp->sf_bsize;
		sbp->f_blocks = (int32_t)sfp->sf_blocks;
		sbp->f_bfree = (int32_t)sfp->sf_bfree;
		sbp->f_bavail = (int32_t)sfp->sf_bavail;
		sbp->f_files = 0;
		sbp->f_ffree = 0;
	}
}

/*
 * Use the fsinfo stuff to update the mount point.
 */
void
nfscl_loadfsinfo(struct nfsmount *nmp, struct nfsfsinfo *fsp)
{

	if ((nmp->nm_wsize == 0 || fsp->fs_wtpref < nmp->nm_wsize) &&
	    fsp->fs_wtpref >= NFS_FABLKSIZE)
		nmp->nm_wsize = (fsp->fs_wtpref + NFS_FABLKSIZE - 1) &
		    ~(NFS_FABLKSIZE - 1);
	if (fsp->fs_wtmax < nmp->nm_wsize && fsp->fs_wtmax > 0) {
		nmp->nm_wsize = fsp->fs_wtmax & ~(NFS_FABLKSIZE - 1);
		if (nmp->nm_wsize == 0)
			nmp->nm_wsize = fsp->fs_wtmax;
	}
	if (nmp->nm_wsize < NFS_FABLKSIZE)
		nmp->nm_wsize = NFS_FABLKSIZE;
	if ((nmp->nm_rsize == 0 || fsp->fs_rtpref < nmp->nm_rsize) &&
	    fsp->fs_rtpref >= NFS_FABLKSIZE)
		nmp->nm_rsize = (fsp->fs_rtpref + NFS_FABLKSIZE - 1) &
		    ~(NFS_FABLKSIZE - 1);
	if (fsp->fs_rtmax < nmp->nm_rsize && fsp->fs_rtmax > 0) {
		nmp->nm_rsize = fsp->fs_rtmax & ~(NFS_FABLKSIZE - 1);
		if (nmp->nm_rsize == 0)
			nmp->nm_rsize = fsp->fs_rtmax;
	}
	if (nmp->nm_rsize < NFS_FABLKSIZE)
		nmp->nm_rsize = NFS_FABLKSIZE;
	if ((nmp->nm_readdirsize == 0 || fsp->fs_dtpref < nmp->nm_readdirsize)
	    && fsp->fs_dtpref >= NFS_DIRBLKSIZ)
		nmp->nm_readdirsize = (fsp->fs_dtpref + NFS_DIRBLKSIZ - 1) &
		    ~(NFS_DIRBLKSIZ - 1);
	if (fsp->fs_rtmax < nmp->nm_readdirsize && fsp->fs_rtmax > 0) {
		nmp->nm_readdirsize = fsp->fs_rtmax & ~(NFS_DIRBLKSIZ - 1);
		if (nmp->nm_readdirsize == 0)
			nmp->nm_readdirsize = fsp->fs_rtmax;
	}
	if (nmp->nm_readdirsize < NFS_DIRBLKSIZ)
		nmp->nm_readdirsize = NFS_DIRBLKSIZ;
	if (fsp->fs_maxfilesize > 0 &&
	    fsp->fs_maxfilesize < nmp->nm_maxfilesize)
		nmp->nm_maxfilesize = fsp->fs_maxfilesize;
	nmp->nm_mountp->mnt_stat.f_iosize = newnfs_iosize(nmp);
	nmp->nm_state |= NFSSTA_GOTFSINFO;
}

/*
 * Lookups source address which should be used to communicate with
 * @nmp and stores it inside @pdst.
 *
 * Returns 0 on success.
 */
u_int8_t *
nfscl_getmyip(struct nfsmount *nmp, struct in6_addr *paddr, int *isinet6p)
{
#if defined(INET6) || defined(INET)
	int error, fibnum;

	fibnum = curthread->td_proc->p_fibnum;
#endif
#ifdef INET
	if (nmp->nm_nam->sa_family == AF_INET) {
		struct sockaddr_in *sin;
		struct nhop4_extended nh_ext;

		sin = (struct sockaddr_in *)nmp->nm_nam;
		CURVNET_SET(CRED_TO_VNET(nmp->nm_sockreq.nr_cred));
		error = fib4_lookup_nh_ext(fibnum, sin->sin_addr, 0, 0,
		    &nh_ext);
		CURVNET_RESTORE();
		if (error != 0)
			return (NULL);

		if ((ntohl(nh_ext.nh_src.s_addr) >> IN_CLASSA_NSHIFT) ==
		    IN_LOOPBACKNET) {
			/* Ignore loopback addresses */
			return (NULL);
		}

		*isinet6p = 0;
		*((struct in_addr *)paddr) = nh_ext.nh_src;

		return (u_int8_t *)paddr;
	}
#endif
#ifdef INET6
	if (nmp->nm_nam->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)nmp->nm_nam;

		CURVNET_SET(CRED_TO_VNET(nmp->nm_sockreq.nr_cred));
		error = in6_selectsrc_addr(fibnum, &sin6->sin6_addr,
		    sin6->sin6_scope_id, NULL, paddr, NULL);
		CURVNET_RESTORE();
		if (error != 0)
			return (NULL);

		if (IN6_IS_ADDR_LOOPBACK(paddr))
			return (NULL);

		/* Scope is embedded in */
		*isinet6p = 1;

		return (u_int8_t *)paddr;
	}
#endif
	return (NULL);
}

/*
 * Copy NFS uid, gids from the cred structure.
 */
void
newnfs_copyincred(struct ucred *cr, struct nfscred *nfscr)
{
	int i;

	KASSERT(cr->cr_ngroups >= 0,
	    ("newnfs_copyincred: negative cr_ngroups"));
	nfscr->nfsc_uid = cr->cr_uid;
	nfscr->nfsc_ngroups = MIN(cr->cr_ngroups, NFS_MAXGRPS + 1);
	for (i = 0; i < nfscr->nfsc_ngroups; i++)
		nfscr->nfsc_groups[i] = cr->cr_groups[i];
}


/*
 * Do any client specific initialization.
 */
void
nfscl_init(void)
{
	static int inited = 0;

	if (inited)
		return;
	inited = 1;
	nfscl_inited = 1;
	ncl_pbuf_zone = pbuf_zsecond_create("nfspbuf", nswbuf / 2);
}

/*
 * Check each of the attributes to be set, to ensure they aren't already
 * the correct value. Disable setting ones already correct.
 */
int
nfscl_checksattr(struct vattr *vap, struct nfsvattr *nvap)
{

	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vap->va_mode == nvap->na_mode)
			vap->va_mode = (mode_t)VNOVAL;
	}
	if (vap->va_uid != (uid_t)VNOVAL) {
		if (vap->va_uid == nvap->na_uid)
			vap->va_uid = (uid_t)VNOVAL;
	}
	if (vap->va_gid != (gid_t)VNOVAL) {
		if (vap->va_gid == nvap->na_gid)
			vap->va_gid = (gid_t)VNOVAL;
	}
	if (vap->va_size != VNOVAL) {
		if (vap->va_size == nvap->na_size)
			vap->va_size = VNOVAL;
	}

	/*
	 * We are normally called with only a partially initialized
	 * VAP.  Since the NFSv3 spec says that server may use the
	 * file attributes to store the verifier, the spec requires
	 * us to do a SETATTR RPC. FreeBSD servers store the verifier
	 * in atime, but we can't really assume that all servers will
	 * so we ensure that our SETATTR sets both atime and mtime.
	 * Set the VA_UTIMES_NULL flag for this case, so that
	 * the server's time will be used.  This is needed to
	 * work around a bug in some Solaris servers, where
	 * setting the time TOCLIENT causes the Setattr RPC
	 * to return NFS_OK, but not set va_mode.
	 */
	if (vap->va_mtime.tv_sec == VNOVAL) {
		vfs_timestamp(&vap->va_mtime);
		vap->va_vaflags |= VA_UTIMES_NULL;
	}
	if (vap->va_atime.tv_sec == VNOVAL)
		vap->va_atime = vap->va_mtime;
	return (1);
}

/*
 * Map nfsv4 errors to errno.h errors.
 * The uid and gid arguments are only used for NFSERR_BADOWNER and that
 * error should only be returned for the Open, Create and Setattr Ops.
 * As such, most calls can just pass in 0 for those arguments.
 */
APPLESTATIC int
nfscl_maperr(struct thread *td, int error, uid_t uid, gid_t gid)
{
	struct proc *p;

	if (error < 10000 || error >= NFSERR_STALEWRITEVERF)
		return (error);
	if (td != NULL)
		p = td->td_proc;
	else
		p = NULL;
	switch (error) {
	case NFSERR_BADOWNER:
		tprintf(p, LOG_INFO,
		    "No name and/or group mapping for uid,gid:(%d,%d)\n",
		    uid, gid);
		return (EPERM);
	case NFSERR_BADNAME:
	case NFSERR_BADCHAR:
		printf("nfsv4 char/name not handled by server\n");
		return (ENOENT);
	case NFSERR_STALECLIENTID:
	case NFSERR_STALESTATEID:
	case NFSERR_EXPIRED:
	case NFSERR_BADSTATEID:
	case NFSERR_BADSESSION:
		printf("nfsv4 recover err returned %d\n", error);
		return (EIO);
	case NFSERR_BADHANDLE:
	case NFSERR_SERVERFAULT:
	case NFSERR_BADTYPE:
	case NFSERR_FHEXPIRED:
	case NFSERR_RESOURCE:
	case NFSERR_MOVED:
	case NFSERR_NOFILEHANDLE:
	case NFSERR_MINORVERMISMATCH:
	case NFSERR_OLDSTATEID:
	case NFSERR_BADSEQID:
	case NFSERR_LEASEMOVED:
	case NFSERR_RECLAIMBAD:
	case NFSERR_BADXDR:
	case NFSERR_OPILLEGAL:
		printf("nfsv4 client/server protocol prob err=%d\n",
		    error);
		return (EIO);
	default:
		tprintf(p, LOG_INFO, "nfsv4 err=%d\n", error);
		return (EIO);
	};
}

/*
 * Check to see if the process for this owner exists. Return 1 if it doesn't
 * and 0 otherwise.
 */
int
nfscl_procdoesntexist(u_int8_t *own)
{
	union {
		u_int32_t	lval;
		u_int8_t	cval[4];
	} tl;
	struct proc *p;
	pid_t pid;
	int i, ret = 0;

	/* For the single open_owner of all 0 bytes, just return 0. */
	for (i = 0; i < NFSV4CL_LOCKNAMELEN; i++)
		if (own[i] != 0)
			break;
	if (i == NFSV4CL_LOCKNAMELEN)
		return (0);

	tl.cval[0] = *own++;
	tl.cval[1] = *own++;
	tl.cval[2] = *own++;
	tl.cval[3] = *own++;
	pid = tl.lval;
	p = pfind(pid);
	if (p == NULL)
		return (1);
	if (p->p_stats == NULL) {
		PROC_UNLOCK(p);
		return (0);
	}
	tl.cval[0] = *own++;
	tl.cval[1] = *own++;
	tl.cval[2] = *own++;
	tl.cval[3] = *own++;
	if (tl.lval != p->p_stats->p_start.tv_sec) {
		ret = 1;
	} else {
		tl.cval[0] = *own++;
		tl.cval[1] = *own++;
		tl.cval[2] = *own++;
		tl.cval[3] = *own;
		if (tl.lval != p->p_stats->p_start.tv_usec)
			ret = 1;
	}
	PROC_UNLOCK(p);
	return (ret);
}

/*
 * - nfs pseudo system call for the client
 */
/*
 * MPSAFE
 */
static int
nfssvc_nfscl(struct thread *td, struct nfssvc_args *uap)
{
	struct file *fp;
	struct nfscbd_args nfscbdarg;
	struct nfsd_nfscbd_args nfscbdarg2;
	struct nameidata nd;
	struct nfscl_dumpmntopts dumpmntopts;
	cap_rights_t rights;
	char *buf;
	int error;
	struct mount *mp;
	struct nfsmount *nmp;

	if (uap->flag & NFSSVC_CBADDSOCK) {
		error = copyin(uap->argp, (caddr_t)&nfscbdarg, sizeof(nfscbdarg));
		if (error)
			return (error);
		/*
		 * Since we don't know what rights might be required,
		 * pretend that we need them all. It is better to be too
		 * careful than too reckless.
		 */
		error = fget(td, nfscbdarg.sock,
		    cap_rights_init(&rights, CAP_SOCK_CLIENT), &fp);
		if (error)
			return (error);
		if (fp->f_type != DTYPE_SOCKET) {
			fdrop(fp, td);
			return (EPERM);
		}
		error = nfscbd_addsock(fp);
		fdrop(fp, td);
		if (!error && nfscl_enablecallb == 0) {
			nfsv4_cbport = nfscbdarg.port;
			nfscl_enablecallb = 1;
		}
	} else if (uap->flag & NFSSVC_NFSCBD) {
		if (uap->argp == NULL) 
			return (EINVAL);
		error = copyin(uap->argp, (caddr_t)&nfscbdarg2,
		    sizeof(nfscbdarg2));
		if (error)
			return (error);
		error = nfscbd_nfsd(td, &nfscbdarg2);
	} else if (uap->flag & NFSSVC_DUMPMNTOPTS) {
		error = copyin(uap->argp, &dumpmntopts, sizeof(dumpmntopts));
		if (error == 0 && (dumpmntopts.ndmnt_blen < 256 ||
		    dumpmntopts.ndmnt_blen > 1024))
			error = EINVAL;
		if (error == 0)
			error = nfsrv_lookupfilename(&nd,
			    dumpmntopts.ndmnt_fname, td);
		if (error == 0 && strcmp(nd.ni_vp->v_mount->mnt_vfc->vfc_name,
		    "nfs") != 0) {
			vput(nd.ni_vp);
			error = EINVAL;
		}
		if (error == 0) {
			buf = malloc(dumpmntopts.ndmnt_blen, M_TEMP, M_WAITOK);
			nfscl_retopts(VFSTONFS(nd.ni_vp->v_mount), buf,
			    dumpmntopts.ndmnt_blen);
			vput(nd.ni_vp);
			error = copyout(buf, dumpmntopts.ndmnt_buf,
			    dumpmntopts.ndmnt_blen);
			free(buf, M_TEMP);
		}
	} else if (uap->flag & NFSSVC_FORCEDISM) {
		buf = malloc(MNAMELEN + 1, M_TEMP, M_WAITOK);
		error = copyinstr(uap->argp, buf, MNAMELEN + 1, NULL);
		if (error == 0) {
			nmp = NULL;
			mtx_lock(&mountlist_mtx);
			TAILQ_FOREACH(mp, &mountlist, mnt_list) {
				if (strcmp(mp->mnt_stat.f_mntonname, buf) ==
				    0 && strcmp(mp->mnt_stat.f_fstypename,
				    "nfs") == 0 && mp->mnt_data != NULL) {
					nmp = VFSTONFS(mp);
					NFSDDSLOCK();
					if (nfsv4_findmirror(nmp) != NULL) {
						NFSDDSUNLOCK();
						error = ENXIO;
						nmp = NULL;
						break;
					}
					mtx_lock(&nmp->nm_mtx);
					if ((nmp->nm_privflag &
					    NFSMNTP_FORCEDISM) == 0) {
						nmp->nm_privflag |= 
						   (NFSMNTP_FORCEDISM |
						    NFSMNTP_CANCELRPCS);
						mtx_unlock(&nmp->nm_mtx);
					} else {
						mtx_unlock(&nmp->nm_mtx);
						nmp = NULL;
					}
					NFSDDSUNLOCK();
					break;
				}
			}
			mtx_unlock(&mountlist_mtx);

			if (nmp != NULL) {
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
				 * Then clear NFSMNTP_CANCELRPCS and wakeup(),
				 * so that nfs_unmount() can complete.
				 */
				newnfs_nmcancelreqs(nmp);
				mtx_lock(&nmp->nm_mtx);
				nmp->nm_privflag &= ~NFSMNTP_CANCELRPCS;
				wakeup(nmp);
				mtx_unlock(&nmp->nm_mtx);
			} else if (error == 0)
				error = EINVAL;
		}
		free(buf, M_TEMP);
	} else {
		error = EINVAL;
	}
	return (error);
}

extern int (*nfsd_call_nfscl)(struct thread *, struct nfssvc_args *);

/*
 * Called once to initialize data structures...
 */
static int
nfscl_modevent(module_t mod, int type, void *data)
{
	int error = 0;
	static int loaded = 0;

	switch (type) {
	case MOD_LOAD:
		if (loaded)
			return (0);
		newnfs_portinit();
		mtx_init(&ncl_iod_mutex, "ncl_iod_mutex", NULL, MTX_DEF);
		nfscl_init();
		NFSD_LOCK();
		nfsrvd_cbinit(0);
		NFSD_UNLOCK();
		ncl_call_invalcaches = ncl_invalcaches;
		nfsd_call_nfscl = nfssvc_nfscl;
		loaded = 1;
		break;

	case MOD_UNLOAD:
		if (nfs_numnfscbd != 0) {
			error = EBUSY;
			break;
		}

		/*
		 * XXX: Unloading of nfscl module is unsupported.
		 */
#if 0
		ncl_call_invalcaches = NULL;
		nfsd_call_nfscl = NULL;
		uma_zdestroy(ncl_pbuf_zone);
		/* and get rid of the mutexes */
		mtx_destroy(&ncl_iod_mutex);
		loaded = 0;
		break;
#else
		/* FALLTHROUGH */
#endif
	default:
		error = EOPNOTSUPP;
		break;
	}
	return error;
}
static moduledata_t nfscl_mod = {
	"nfscl",
	nfscl_modevent,
	NULL,
};
DECLARE_MODULE(nfscl, nfscl_mod, SI_SUB_VFS, SI_ORDER_FIRST);

/* So that loader and kldload(2) can find us, wherever we are.. */
MODULE_VERSION(nfscl, 1);
MODULE_DEPEND(nfscl, nfscommon, 1, 1, 1);
MODULE_DEPEND(nfscl, krpc, 1, 1, 1);
MODULE_DEPEND(nfscl, nfssvc, 1, 1, 1);
MODULE_DEPEND(nfscl, nfslock, 1, 1, 1);

