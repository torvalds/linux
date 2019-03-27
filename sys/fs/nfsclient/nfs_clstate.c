/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Rick Macklem, University of Guelph
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/*
 * These functions implement the client side state handling for NFSv4.
 * NFSv4 state handling:
 * - A lockowner is used to determine lock contention, so it
 *   corresponds directly to a Posix pid. (1 to 1 mapping)
 * - The correct granularity of an OpenOwner is not nearly so
 *   obvious. An OpenOwner does the following:
 *   - provides a serial sequencing of Open/Close/Lock-with-new-lockowner
 *   - is used to check for Open/Share contention (not applicable to
 *     this client, since all Opens are Deny_None)
 *   As such, I considered both extreme.
 *   1 OpenOwner per ClientID - Simple to manage, but fully serializes
 *   all Open, Close and Lock (with a new lockowner) Ops.
 *   1 OpenOwner for each Open - This one results in an OpenConfirm for
 *   every Open, for most servers.
 *   So, I chose to use the same mapping as I did for LockOwnwers.
 *   The main concern here is that you can end up with multiple Opens
 *   for the same File Handle, but on different OpenOwners (opens
 *   inherited from parents, grandparents...) and you do not know
 *   which of these the vnodeop close applies to. This is handled by
 *   delaying the Close Op(s) until all of the Opens have been closed.
 *   (It is not yet obvious if this is the correct granularity.)
 * - How the code handles serialization:
 *   - For the ClientId, it uses an exclusive lock while getting its
 *     SetClientId and during recovery. Otherwise, it uses a shared
 *     lock via a reference count.
 *   - For the rest of the data structures, it uses an SMP mutex
 *     (once the nfs client is SMP safe) and doesn't sleep while
 *     manipulating the linked lists.
 *   - The serialization of Open/Close/Lock/LockU falls out in the
 *     "wash", since OpenOwners and LockOwners are both mapped from
 *     Posix pid. In other words, there is only one Posix pid using
 *     any given owner, so that owner is serialized. (If you change
 *     the granularity of the OpenOwner, then code must be added to
 *     serialize Ops on the OpenOwner.)
 * - When to get rid of OpenOwners and LockOwners.
 *   - The function nfscl_cleanup_common() is executed after a process exits.
 *     It goes through the client list looking for all Open and Lock Owners.
 *     When one is found, it is marked "defunct" or in the case of
 *     an OpenOwner without any Opens, freed.
 *     The renew thread scans for defunct Owners and gets rid of them,
 *     if it can. The LockOwners will also be deleted when the
 *     associated Open is closed.
 *   - If the LockU or Close Op(s) fail during close in a way
 *     that could be recovered upon retry, they are relinked to the
 *     ClientId's defunct open list and retried by the renew thread
 *     until they succeed or an unmount/recovery occurs.
 *     (Since we are done with them, they do not need to be recovered.)
 */

#ifndef APPLEKEXT
#include <fs/nfs/nfsport.h>

/*
 * Global variables
 */
extern struct nfsstatsv1 nfsstatsv1;
extern struct nfsreqhead nfsd_reqq;
extern u_int32_t newnfs_false, newnfs_true;
extern int nfscl_debuglevel;
extern int nfscl_enablecallb;
extern int nfs_numnfscbd;
NFSREQSPINLOCK;
NFSCLSTATEMUTEX;
int nfscl_inited = 0;
struct nfsclhead nfsclhead;	/* Head of clientid list */
int nfscl_deleghighwater = NFSCLDELEGHIGHWATER;
int nfscl_layouthighwater = NFSCLLAYOUTHIGHWATER;
#endif	/* !APPLEKEXT */

static int nfscl_delegcnt = 0;
static int nfscl_layoutcnt = 0;
static int nfscl_getopen(struct nfsclownerhead *, u_int8_t *, int, u_int8_t *,
    u_int8_t *, u_int32_t, struct nfscllockowner **, struct nfsclopen **);
static void nfscl_clrelease(struct nfsclclient *);
static void nfscl_cleanclient(struct nfsclclient *);
static void nfscl_expireclient(struct nfsclclient *, struct nfsmount *,
    struct ucred *, NFSPROC_T *);
static int nfscl_expireopen(struct nfsclclient *, struct nfsclopen *,
    struct nfsmount *, struct ucred *, NFSPROC_T *);
static void nfscl_recover(struct nfsclclient *, struct ucred *, NFSPROC_T *);
static void nfscl_insertlock(struct nfscllockowner *, struct nfscllock *,
    struct nfscllock *, int);
static int nfscl_updatelock(struct nfscllockowner *, struct nfscllock **,
    struct nfscllock **, int);
static void nfscl_delegreturnall(struct nfsclclient *, NFSPROC_T *);
static u_int32_t nfscl_nextcbident(void);
static mount_t nfscl_getmnt(int, uint8_t *, u_int32_t, struct nfsclclient **);
static struct nfsclclient *nfscl_getclnt(u_int32_t);
static struct nfsclclient *nfscl_getclntsess(uint8_t *);
static struct nfscldeleg *nfscl_finddeleg(struct nfsclclient *, u_int8_t *,
    int);
static void nfscl_retoncloselayout(vnode_t, struct nfsclclient *, uint8_t *,
    int, struct nfsclrecalllayout **);
static void nfscl_reldevinfo_locked(struct nfscldevinfo *);
static struct nfscllayout *nfscl_findlayout(struct nfsclclient *, u_int8_t *,
    int);
static struct nfscldevinfo *nfscl_finddevinfo(struct nfsclclient *, uint8_t *);
static int nfscl_checkconflict(struct nfscllockownerhead *, struct nfscllock *,
    u_int8_t *, struct nfscllock **);
static void nfscl_freealllocks(struct nfscllockownerhead *, int);
static int nfscl_localconflict(struct nfsclclient *, u_int8_t *, int,
    struct nfscllock *, u_int8_t *, struct nfscldeleg *, struct nfscllock **);
static void nfscl_newopen(struct nfsclclient *, struct nfscldeleg *,
    struct nfsclowner **, struct nfsclowner **, struct nfsclopen **,
    struct nfsclopen **, u_int8_t *, u_int8_t *, int, struct ucred *, int *);
static int nfscl_moveopen(vnode_t , struct nfsclclient *,
    struct nfsmount *, struct nfsclopen *, struct nfsclowner *,
    struct nfscldeleg *, struct ucred *, NFSPROC_T *);
static void nfscl_totalrecall(struct nfsclclient *);
static int nfscl_relock(vnode_t , struct nfsclclient *, struct nfsmount *,
    struct nfscllockowner *, struct nfscllock *, struct ucred *, NFSPROC_T *);
static int nfscl_tryopen(struct nfsmount *, vnode_t , u_int8_t *, int,
    u_int8_t *, int, u_int32_t, struct nfsclopen *, u_int8_t *, int,
    struct nfscldeleg **, int, u_int32_t, struct ucred *, NFSPROC_T *);
static int nfscl_trylock(struct nfsmount *, vnode_t , u_int8_t *,
    int, struct nfscllockowner *, int, int, u_int64_t, u_int64_t, short,
    struct ucred *, NFSPROC_T *);
static int nfsrpc_reopen(struct nfsmount *, u_int8_t *, int, u_int32_t,
    struct nfsclopen *, struct nfscldeleg **, struct ucred *, NFSPROC_T *);
static void nfscl_freedeleg(struct nfscldeleghead *, struct nfscldeleg *);
static int nfscl_errmap(struct nfsrv_descript *, u_int32_t);
static void nfscl_cleanup_common(struct nfsclclient *, u_int8_t *);
static int nfscl_recalldeleg(struct nfsclclient *, struct nfsmount *,
    struct nfscldeleg *, vnode_t, struct ucred *, NFSPROC_T *, int);
static void nfscl_freeopenowner(struct nfsclowner *, int);
static void nfscl_cleandeleg(struct nfscldeleg *);
static int nfscl_trydelegreturn(struct nfscldeleg *, struct ucred *,
    struct nfsmount *, NFSPROC_T *);
static void nfscl_emptylockowner(struct nfscllockowner *,
    struct nfscllockownerfhhead *);
static void nfscl_mergeflayouts(struct nfsclflayouthead *,
    struct nfsclflayouthead *);
static int nfscl_layoutrecall(int, struct nfscllayout *, uint32_t, uint64_t,
    uint64_t, uint32_t, uint32_t, uint32_t, char *, struct nfsclrecalllayout *);
static int nfscl_seq(uint32_t, uint32_t);
static void nfscl_layoutreturn(struct nfsmount *, struct nfscllayout *,
    struct ucred *, NFSPROC_T *);
static void nfscl_dolayoutcommit(struct nfsmount *, struct nfscllayout *,
    struct ucred *, NFSPROC_T *);

static short nfscberr_null[] = {
	0,
	0,
};

static short nfscberr_getattr[] = {
	NFSERR_RESOURCE,
	NFSERR_BADHANDLE,
	NFSERR_BADXDR,
	NFSERR_RESOURCE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfscberr_recall[] = {
	NFSERR_RESOURCE,
	NFSERR_BADHANDLE,
	NFSERR_BADSTATEID,
	NFSERR_BADXDR,
	NFSERR_RESOURCE,
	NFSERR_SERVERFAULT,
	0,
};

static short *nfscl_cberrmap[] = {
	nfscberr_null,
	nfscberr_null,
	nfscberr_null,
	nfscberr_getattr,
	nfscberr_recall
};

#define	NETFAMILY(clp) \
		(((clp)->nfsc_flags & NFSCLFLAGS_AFINET6) ? AF_INET6 : AF_INET)

/*
 * Called for an open operation.
 * If the nfhp argument is NULL, just get an openowner.
 */
APPLESTATIC int
nfscl_open(vnode_t vp, u_int8_t *nfhp, int fhlen, u_int32_t amode, int usedeleg,
    struct ucred *cred, NFSPROC_T *p, struct nfsclowner **owpp,
    struct nfsclopen **opp, int *newonep, int *retp, int lockit)
{
	struct nfsclclient *clp;
	struct nfsclowner *owp, *nowp;
	struct nfsclopen *op = NULL, *nop = NULL;
	struct nfscldeleg *dp;
	struct nfsclownerhead *ohp;
	u_int8_t own[NFSV4CL_LOCKNAMELEN];
	int ret;

	if (newonep != NULL)
		*newonep = 0;
	if (opp != NULL)
		*opp = NULL;
	if (owpp != NULL)
		*owpp = NULL;

	/*
	 * Might need one or both of these, so MALLOC them now, to
	 * avoid a tsleep() in MALLOC later.
	 */
	nowp = malloc(sizeof (struct nfsclowner),
	    M_NFSCLOWNER, M_WAITOK);
	if (nfhp != NULL)
	    nop = malloc(sizeof (struct nfsclopen) +
		fhlen - 1, M_NFSCLOPEN, M_WAITOK);
	ret = nfscl_getcl(vnode_mount(vp), cred, p, 1, &clp);
	if (ret != 0) {
		free(nowp, M_NFSCLOWNER);
		if (nop != NULL)
			free(nop, M_NFSCLOPEN);
		return (ret);
	}

	/*
	 * Get the Open iff it already exists.
	 * If none found, add the new one or return error, depending upon
	 * "create".
	 */
	NFSLOCKCLSTATE();
	dp = NULL;
	/* First check the delegation list */
	if (nfhp != NULL && usedeleg) {
		LIST_FOREACH(dp, NFSCLDELEGHASH(clp, nfhp, fhlen), nfsdl_hash) {
			if (dp->nfsdl_fhlen == fhlen &&
			    !NFSBCMP(nfhp, dp->nfsdl_fh, fhlen)) {
				if (!(amode & NFSV4OPEN_ACCESSWRITE) ||
				    (dp->nfsdl_flags & NFSCLDL_WRITE))
					break;
				dp = NULL;
				break;
			}
		}
	}

	if (dp != NULL) {
		nfscl_filllockowner(p->td_proc, own, F_POSIX);
		ohp = &dp->nfsdl_owner;
	} else {
		/* For NFSv4.1 and this option, use a single open_owner. */
		if (NFSHASONEOPENOWN(VFSTONFS(vnode_mount(vp))))
			nfscl_filllockowner(NULL, own, F_POSIX);
		else
			nfscl_filllockowner(p->td_proc, own, F_POSIX);
		ohp = &clp->nfsc_owner;
	}
	/* Now, search for an openowner */
	LIST_FOREACH(owp, ohp, nfsow_list) {
		if (!NFSBCMP(owp->nfsow_owner, own, NFSV4CL_LOCKNAMELEN))
			break;
	}

	/*
	 * Create a new open, as required.
	 */
	nfscl_newopen(clp, dp, &owp, &nowp, &op, &nop, own, nfhp, fhlen,
	    cred, newonep);

	/*
	 * Now, check the mode on the open and return the appropriate
	 * value.
	 */
	if (retp != NULL) {
		if (nfhp != NULL && dp != NULL && nop == NULL)
			/* new local open on delegation */
			*retp = NFSCLOPEN_SETCRED;
		else
			*retp = NFSCLOPEN_OK;
	}
	if (op != NULL && (amode & ~(op->nfso_mode))) {
		op->nfso_mode |= amode;
		if (retp != NULL && dp == NULL)
			*retp = NFSCLOPEN_DOOPEN;
	}

	/*
	 * Serialize modifications to the open owner for multiple threads
	 * within the same process using a read/write sleep lock.
	 * For NFSv4.1 and a single OpenOwner, allow concurrent open operations
	 * by acquiring a shared lock.  The close operations still use an
	 * exclusive lock for this case.
	 */
	if (lockit != 0) {
		if (NFSHASONEOPENOWN(VFSTONFS(vnode_mount(vp)))) {
			/*
			 * Get a shared lock on the OpenOwner, but first
			 * wait for any pending exclusive lock, so that the
			 * exclusive locker gets priority.
			 */
			nfsv4_lock(&owp->nfsow_rwlock, 0, NULL,
			    NFSCLSTATEMUTEXPTR, NULL);
			nfsv4_getref(&owp->nfsow_rwlock, NULL,
			    NFSCLSTATEMUTEXPTR, NULL);
		} else
			nfscl_lockexcl(&owp->nfsow_rwlock, NFSCLSTATEMUTEXPTR);
	}
	NFSUNLOCKCLSTATE();
	if (nowp != NULL)
		free(nowp, M_NFSCLOWNER);
	if (nop != NULL)
		free(nop, M_NFSCLOPEN);
	if (owpp != NULL)
		*owpp = owp;
	if (opp != NULL)
		*opp = op;
	return (0);
}

/*
 * Create a new open, as required.
 */
static void
nfscl_newopen(struct nfsclclient *clp, struct nfscldeleg *dp,
    struct nfsclowner **owpp, struct nfsclowner **nowpp, struct nfsclopen **opp,
    struct nfsclopen **nopp, u_int8_t *own, u_int8_t *fhp, int fhlen,
    struct ucred *cred, int *newonep)
{
	struct nfsclowner *owp = *owpp, *nowp;
	struct nfsclopen *op, *nop;

	if (nowpp != NULL)
		nowp = *nowpp;
	else
		nowp = NULL;
	if (nopp != NULL)
		nop = *nopp;
	else
		nop = NULL;
	if (owp == NULL && nowp != NULL) {
		NFSBCOPY(own, nowp->nfsow_owner, NFSV4CL_LOCKNAMELEN);
		LIST_INIT(&nowp->nfsow_open);
		nowp->nfsow_clp = clp;
		nowp->nfsow_seqid = 0;
		nowp->nfsow_defunct = 0;
		nfscl_lockinit(&nowp->nfsow_rwlock);
		if (dp != NULL) {
			nfsstatsv1.cllocalopenowners++;
			LIST_INSERT_HEAD(&dp->nfsdl_owner, nowp, nfsow_list);
		} else {
			nfsstatsv1.clopenowners++;
			LIST_INSERT_HEAD(&clp->nfsc_owner, nowp, nfsow_list);
		}
		owp = *owpp = nowp;
		*nowpp = NULL;
		if (newonep != NULL)
			*newonep = 1;
	}

	 /* If an fhp has been specified, create an Open as well. */
	if (fhp != NULL) {
		/* and look for the correct open, based upon FH */
		LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
			if (op->nfso_fhlen == fhlen &&
			    !NFSBCMP(op->nfso_fh, fhp, fhlen))
				break;
		}
		if (op == NULL && nop != NULL) {
			nop->nfso_own = owp;
			nop->nfso_mode = 0;
			nop->nfso_opencnt = 0;
			nop->nfso_posixlock = 1;
			nop->nfso_fhlen = fhlen;
			NFSBCOPY(fhp, nop->nfso_fh, fhlen);
			LIST_INIT(&nop->nfso_lock);
			nop->nfso_stateid.seqid = 0;
			nop->nfso_stateid.other[0] = 0;
			nop->nfso_stateid.other[1] = 0;
			nop->nfso_stateid.other[2] = 0;
			KASSERT(cred != NULL, ("%s: cred NULL\n", __func__));
			newnfs_copyincred(cred, &nop->nfso_cred);
			if (dp != NULL) {
				TAILQ_REMOVE(&clp->nfsc_deleg, dp, nfsdl_list);
				TAILQ_INSERT_HEAD(&clp->nfsc_deleg, dp,
				    nfsdl_list);
				dp->nfsdl_timestamp = NFSD_MONOSEC + 120;
				nfsstatsv1.cllocalopens++;
			} else {
				nfsstatsv1.clopens++;
			}
			LIST_INSERT_HEAD(&owp->nfsow_open, nop, nfso_list);
			*opp = nop;
			*nopp = NULL;
			if (newonep != NULL)
				*newonep = 1;
		} else {
			*opp = op;
		}
	}
}

/*
 * Called to find/add a delegation to a client.
 */
APPLESTATIC int
nfscl_deleg(mount_t mp, struct nfsclclient *clp, u_int8_t *nfhp,
    int fhlen, struct ucred *cred, NFSPROC_T *p, struct nfscldeleg **dpp)
{
	struct nfscldeleg *dp = *dpp, *tdp;

	/*
	 * First, if we have received a Read delegation for a file on a
	 * read/write file system, just return it, because they aren't
	 * useful, imho.
	 */
	if (mp != NULL && dp != NULL && !NFSMNT_RDONLY(mp) &&
	    (dp->nfsdl_flags & NFSCLDL_READ)) {
		(void) nfscl_trydelegreturn(dp, cred, VFSTONFS(mp), p);
		free(dp, M_NFSCLDELEG);
		*dpp = NULL;
		return (0);
	}

	/* Look for the correct deleg, based upon FH */
	NFSLOCKCLSTATE();
	tdp = nfscl_finddeleg(clp, nfhp, fhlen);
	if (tdp == NULL) {
		if (dp == NULL) {
			NFSUNLOCKCLSTATE();
			return (NFSERR_BADSTATEID);
		}
		*dpp = NULL;
		TAILQ_INSERT_HEAD(&clp->nfsc_deleg, dp, nfsdl_list);
		LIST_INSERT_HEAD(NFSCLDELEGHASH(clp, nfhp, fhlen), dp,
		    nfsdl_hash);
		dp->nfsdl_timestamp = NFSD_MONOSEC + 120;
		nfsstatsv1.cldelegates++;
		nfscl_delegcnt++;
	} else {
		/*
		 * Delegation already exists, what do we do if a new one??
		 */
		if (dp != NULL) {
			printf("Deleg already exists!\n");
			free(dp, M_NFSCLDELEG);
			*dpp = NULL;
		} else {
			*dpp = tdp;
		}
	}
	NFSUNLOCKCLSTATE();
	return (0);
}

/*
 * Find a delegation for this file handle. Return NULL upon failure.
 */
static struct nfscldeleg *
nfscl_finddeleg(struct nfsclclient *clp, u_int8_t *fhp, int fhlen)
{
	struct nfscldeleg *dp;

	LIST_FOREACH(dp, NFSCLDELEGHASH(clp, fhp, fhlen), nfsdl_hash) {
	    if (dp->nfsdl_fhlen == fhlen &&
		!NFSBCMP(dp->nfsdl_fh, fhp, fhlen))
		break;
	}
	return (dp);
}

/*
 * Get a stateid for an I/O operation. First, look for an open and iff
 * found, return either a lockowner stateid or the open stateid.
 * If no Open is found, just return error and the special stateid of all zeros.
 */
APPLESTATIC int
nfscl_getstateid(vnode_t vp, u_int8_t *nfhp, int fhlen, u_int32_t mode,
    int fords, struct ucred *cred, NFSPROC_T *p, nfsv4stateid_t *stateidp,
    void **lckpp)
{
	struct nfsclclient *clp;
	struct nfsclowner *owp;
	struct nfsclopen *op = NULL, *top;
	struct nfscllockowner *lp;
	struct nfscldeleg *dp;
	struct nfsnode *np;
	struct nfsmount *nmp;
	u_int8_t own[NFSV4CL_LOCKNAMELEN];
	int error, done;

	*lckpp = NULL;
	/*
	 * Initially, just set the special stateid of all zeros.
	 * (Don't do this for a DS, since the special stateid can't be used.)
	 */
	if (fords == 0) {
		stateidp->seqid = 0;
		stateidp->other[0] = 0;
		stateidp->other[1] = 0;
		stateidp->other[2] = 0;
	}
	if (vnode_vtype(vp) != VREG)
		return (EISDIR);
	np = VTONFS(vp);
	nmp = VFSTONFS(vnode_mount(vp));
	NFSLOCKCLSTATE();
	clp = nfscl_findcl(nmp);
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return (EACCES);
	}

	/*
	 * Wait for recovery to complete.
	 */
	while ((clp->nfsc_flags & NFSCLFLAGS_RECVRINPROG))
		(void) nfsmsleep(&clp->nfsc_flags, NFSCLSTATEMUTEXPTR,
		    PZERO, "nfsrecvr", NULL);

	/*
	 * First, look for a delegation.
	 */
	LIST_FOREACH(dp, NFSCLDELEGHASH(clp, nfhp, fhlen), nfsdl_hash) {
		if (dp->nfsdl_fhlen == fhlen &&
		    !NFSBCMP(nfhp, dp->nfsdl_fh, fhlen)) {
			if (!(mode & NFSV4OPEN_ACCESSWRITE) ||
			    (dp->nfsdl_flags & NFSCLDL_WRITE)) {
				stateidp->seqid = dp->nfsdl_stateid.seqid;
				stateidp->other[0] = dp->nfsdl_stateid.other[0];
				stateidp->other[1] = dp->nfsdl_stateid.other[1];
				stateidp->other[2] = dp->nfsdl_stateid.other[2];
				if (!(np->n_flag & NDELEGRECALL)) {
					TAILQ_REMOVE(&clp->nfsc_deleg, dp,
					    nfsdl_list);
					TAILQ_INSERT_HEAD(&clp->nfsc_deleg, dp,
					    nfsdl_list);
					dp->nfsdl_timestamp = NFSD_MONOSEC +
					    120;
					dp->nfsdl_rwlock.nfslock_usecnt++;
					*lckpp = (void *)&dp->nfsdl_rwlock;
				}
				NFSUNLOCKCLSTATE();
				return (0);
			}
			break;
		}
	}

	if (p != NULL) {
		/*
		 * If p != NULL, we want to search the parentage tree
		 * for a matching OpenOwner and use that.
		 */
		if (NFSHASONEOPENOWN(VFSTONFS(vnode_mount(vp))))
			nfscl_filllockowner(NULL, own, F_POSIX);
		else
			nfscl_filllockowner(p->td_proc, own, F_POSIX);
		lp = NULL;
		error = nfscl_getopen(&clp->nfsc_owner, nfhp, fhlen, own, own,
		    mode, &lp, &op);
		if (error == 0 && lp != NULL && fords == 0) {
			/* Don't return a lock stateid for a DS. */
			stateidp->seqid =
			    lp->nfsl_stateid.seqid;
			stateidp->other[0] =
			    lp->nfsl_stateid.other[0];
			stateidp->other[1] =
			    lp->nfsl_stateid.other[1];
			stateidp->other[2] =
			    lp->nfsl_stateid.other[2];
			NFSUNLOCKCLSTATE();
			return (0);
		}
	}
	if (op == NULL) {
		/* If not found, just look for any OpenOwner that will work. */
		top = NULL;
		done = 0;
		owp = LIST_FIRST(&clp->nfsc_owner);
		while (!done && owp != NULL) {
			LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
				if (op->nfso_fhlen == fhlen &&
				    !NFSBCMP(op->nfso_fh, nfhp, fhlen)) {
					if (top == NULL && (op->nfso_mode &
					    NFSV4OPEN_ACCESSWRITE) != 0 &&
					    (mode & NFSV4OPEN_ACCESSREAD) != 0)
						top = op;
					if ((mode & op->nfso_mode) == mode) {
						done = 1;
						break;
					}
				}
			}
			if (!done)
				owp = LIST_NEXT(owp, nfsow_list);
		}
		if (!done) {
			NFSCL_DEBUG(2, "openmode top=%p\n", top);
			if (top == NULL || NFSHASOPENMODE(nmp)) {
				NFSUNLOCKCLSTATE();
				return (ENOENT);
			} else
				op = top;
		}
		/*
		 * For read aheads or write behinds, use the open cred.
		 * A read ahead or write behind is indicated by p == NULL.
		 */
		if (p == NULL)
			newnfs_copycred(&op->nfso_cred, cred);
	}

	/*
	 * No lock stateid, so return the open stateid.
	 */
	stateidp->seqid = op->nfso_stateid.seqid;
	stateidp->other[0] = op->nfso_stateid.other[0];
	stateidp->other[1] = op->nfso_stateid.other[1];
	stateidp->other[2] = op->nfso_stateid.other[2];
	NFSUNLOCKCLSTATE();
	return (0);
}

/*
 * Search for a matching file, mode and, optionally, lockowner.
 */
static int
nfscl_getopen(struct nfsclownerhead *ohp, u_int8_t *nfhp, int fhlen,
    u_int8_t *openown, u_int8_t *lockown, u_int32_t mode,
    struct nfscllockowner **lpp, struct nfsclopen **opp)
{
	struct nfsclowner *owp;
	struct nfsclopen *op, *rop, *rop2;
	struct nfscllockowner *lp;
	int keep_looping;

	if (lpp != NULL)
		*lpp = NULL;
	/*
	 * rop will be set to the open to be returned. There are three
	 * variants of this, all for an open of the correct file:
	 * 1 - A match of lockown.
	 * 2 - A match of the openown, when no lockown match exists.
	 * 3 - A match for any open, if no openown or lockown match exists.
	 * Looking for #2 over #3 probably isn't necessary, but since
	 * RFC3530 is vague w.r.t. the relationship between openowners and
	 * lockowners, I think this is the safer way to go.
	 */
	rop = NULL;
	rop2 = NULL;
	keep_looping = 1;
	/* Search the client list */
	owp = LIST_FIRST(ohp);
	while (owp != NULL && keep_looping != 0) {
		/* and look for the correct open */
		op = LIST_FIRST(&owp->nfsow_open);
		while (op != NULL && keep_looping != 0) {
			if (op->nfso_fhlen == fhlen &&
			    !NFSBCMP(op->nfso_fh, nfhp, fhlen)
			    && (op->nfso_mode & mode) == mode) {
				if (lpp != NULL) {
					/* Now look for a matching lockowner. */
					LIST_FOREACH(lp, &op->nfso_lock,
					    nfsl_list) {
						if (!NFSBCMP(lp->nfsl_owner,
						    lockown,
						    NFSV4CL_LOCKNAMELEN)) {
							*lpp = lp;
							rop = op;
							keep_looping = 0;
							break;
						}
					}
				}
				if (rop == NULL && !NFSBCMP(owp->nfsow_owner,
				    openown, NFSV4CL_LOCKNAMELEN)) {
					rop = op;
					if (lpp == NULL)
						keep_looping = 0;
				}
				if (rop2 == NULL)
					rop2 = op;
			}
			op = LIST_NEXT(op, nfso_list);
		}
		owp = LIST_NEXT(owp, nfsow_list);
	}
	if (rop == NULL)
		rop = rop2;
	if (rop == NULL)
		return (EBADF);
	*opp = rop;
	return (0);
}

/*
 * Release use of an open owner. Called when open operations are done
 * with the open owner.
 */
APPLESTATIC void
nfscl_ownerrelease(struct nfsmount *nmp, struct nfsclowner *owp,
    __unused int error, __unused int candelete, int unlocked)
{

	if (owp == NULL)
		return;
	NFSLOCKCLSTATE();
	if (unlocked == 0) {
		if (NFSHASONEOPENOWN(nmp))
			nfsv4_relref(&owp->nfsow_rwlock);
		else
			nfscl_lockunlock(&owp->nfsow_rwlock);
	}
	nfscl_clrelease(owp->nfsow_clp);
	NFSUNLOCKCLSTATE();
}

/*
 * Release use of an open structure under an open owner.
 */
APPLESTATIC void
nfscl_openrelease(struct nfsmount *nmp, struct nfsclopen *op, int error,
    int candelete)
{
	struct nfsclclient *clp;
	struct nfsclowner *owp;

	if (op == NULL)
		return;
	NFSLOCKCLSTATE();
	owp = op->nfso_own;
	if (NFSHASONEOPENOWN(nmp))
		nfsv4_relref(&owp->nfsow_rwlock);
	else
		nfscl_lockunlock(&owp->nfsow_rwlock);
	clp = owp->nfsow_clp;
	if (error && candelete && op->nfso_opencnt == 0)
		nfscl_freeopen(op, 0);
	nfscl_clrelease(clp);
	NFSUNLOCKCLSTATE();
}

/*
 * Called to get a clientid structure. It will optionally lock the
 * client data structures to do the SetClientId/SetClientId_confirm,
 * but will release that lock and return the clientid with a reference
 * count on it.
 * If the "cred" argument is NULL, a new clientid should not be created.
 * If the "p" argument is NULL, a SetClientID/SetClientIDConfirm cannot
 * be done.
 * The start_renewthread argument tells nfscl_getcl() to start a renew
 * thread if this creates a new clp.
 * It always clpp with a reference count on it, unless returning an error.
 */
APPLESTATIC int
nfscl_getcl(struct mount *mp, struct ucred *cred, NFSPROC_T *p,
    int start_renewthread, struct nfsclclient **clpp)
{
	struct nfsclclient *clp;
	struct nfsclclient *newclp = NULL;
	struct nfsmount *nmp;
	char uuid[HOSTUUIDLEN];
	int igotlock = 0, error, trystalecnt, clidinusedelay, i;
	u_int16_t idlen = 0;

	nmp = VFSTONFS(mp);
	if (cred != NULL) {
		getcredhostuuid(cred, uuid, sizeof uuid);
		idlen = strlen(uuid);
		if (idlen > 0)
			idlen += sizeof (u_int64_t);
		else
			idlen += sizeof (u_int64_t) + 16; /* 16 random bytes */
		newclp = malloc(
		    sizeof (struct nfsclclient) + idlen - 1, M_NFSCLCLIENT,
		    M_WAITOK | M_ZERO);
	}
	NFSLOCKCLSTATE();
	/*
	 * If a forced dismount is already in progress, don't
	 * allocate a new clientid and get out now. For the case where
	 * clp != NULL, this is a harmless optimization.
	 */
	if (NFSCL_FORCEDISM(mp)) {
		NFSUNLOCKCLSTATE();
		if (newclp != NULL)
			free(newclp, M_NFSCLCLIENT);
		return (EBADF);
	}
	clp = nmp->nm_clp;
	if (clp == NULL) {
		if (newclp == NULL) {
			NFSUNLOCKCLSTATE();
			return (EACCES);
		}
		clp = newclp;
		clp->nfsc_idlen = idlen;
		LIST_INIT(&clp->nfsc_owner);
		TAILQ_INIT(&clp->nfsc_deleg);
		TAILQ_INIT(&clp->nfsc_layout);
		LIST_INIT(&clp->nfsc_devinfo);
		for (i = 0; i < NFSCLDELEGHASHSIZE; i++)
			LIST_INIT(&clp->nfsc_deleghash[i]);
		for (i = 0; i < NFSCLLAYOUTHASHSIZE; i++)
			LIST_INIT(&clp->nfsc_layouthash[i]);
		clp->nfsc_flags = NFSCLFLAGS_INITED;
		clp->nfsc_clientidrev = 1;
		clp->nfsc_cbident = nfscl_nextcbident();
		nfscl_fillclid(nmp->nm_clval, uuid, clp->nfsc_id,
		    clp->nfsc_idlen);
		LIST_INSERT_HEAD(&nfsclhead, clp, nfsc_list);
		nmp->nm_clp = clp;
		clp->nfsc_nmp = nmp;
		NFSUNLOCKCLSTATE();
		if (start_renewthread != 0)
			nfscl_start_renewthread(clp);
	} else {
		NFSUNLOCKCLSTATE();
		if (newclp != NULL)
			free(newclp, M_NFSCLCLIENT);
	}
	NFSLOCKCLSTATE();
	while ((clp->nfsc_flags & NFSCLFLAGS_HASCLIENTID) == 0 && !igotlock &&
	    !NFSCL_FORCEDISM(mp))
		igotlock = nfsv4_lock(&clp->nfsc_lock, 1, NULL,
		    NFSCLSTATEMUTEXPTR, mp);
	if (igotlock == 0) {
		/*
		 * Call nfsv4_lock() with "iwantlock == 0" so that it will
		 * wait for a pending exclusive lock request.  This gives the
		 * exclusive lock request priority over this shared lock
		 * request.
		 * An exclusive lock on nfsc_lock is used mainly for server
		 * crash recoveries.
		 */
		nfsv4_lock(&clp->nfsc_lock, 0, NULL, NFSCLSTATEMUTEXPTR, mp);
		nfsv4_getref(&clp->nfsc_lock, NULL, NFSCLSTATEMUTEXPTR, mp);
	}
	if (igotlock == 0 && NFSCL_FORCEDISM(mp)) {
		/*
		 * Both nfsv4_lock() and nfsv4_getref() know to check
		 * for NFSCL_FORCEDISM() and return without sleeping to
		 * wait for the exclusive lock to be released, since it
		 * might be held by nfscl_umount() and we need to get out
		 * now for that case and not wait until nfscl_umount()
		 * releases it.
		 */
		NFSUNLOCKCLSTATE();
		return (EBADF);
	}
	NFSUNLOCKCLSTATE();

	/*
	 * If it needs a clientid, do the setclientid now.
	 */
	if ((clp->nfsc_flags & NFSCLFLAGS_HASCLIENTID) == 0) {
		if (!igotlock)
			panic("nfscl_clget");
		if (p == NULL || cred == NULL) {
			NFSLOCKCLSTATE();
			nfsv4_unlock(&clp->nfsc_lock, 0);
			NFSUNLOCKCLSTATE();
			return (EACCES);
		}
		/*
		 * If RFC3530 Sec. 14.2.33 is taken literally,
		 * NFSERR_CLIDINUSE will be returned persistently for the
		 * case where a new mount of the same file system is using
		 * a different principal. In practice, NFSERR_CLIDINUSE is
		 * only returned when there is outstanding unexpired state
		 * on the clientid. As such, try for twice the lease
		 * interval, if we know what that is. Otherwise, make a
		 * wild ass guess.
		 * The case of returning NFSERR_STALECLIENTID is far less
		 * likely, but might occur if there is a significant delay
		 * between doing the SetClientID and SetClientIDConfirm Ops,
		 * such that the server throws away the clientid before
		 * receiving the SetClientIDConfirm.
		 */
		if (clp->nfsc_renew > 0)
			clidinusedelay = NFSCL_LEASE(clp->nfsc_renew) * 2;
		else
			clidinusedelay = 120;
		trystalecnt = 3;
		do {
			error = nfsrpc_setclient(nmp, clp, 0, cred, p);
			if (error == NFSERR_STALECLIENTID ||
			    error == NFSERR_STALEDONTRECOVER ||
			    error == NFSERR_BADSESSION ||
			    error == NFSERR_CLIDINUSE) {
				(void) nfs_catnap(PZERO, error, "nfs_setcl");
			}
		} while (((error == NFSERR_STALECLIENTID ||
		     error == NFSERR_BADSESSION ||
		     error == NFSERR_STALEDONTRECOVER) && --trystalecnt > 0) ||
		    (error == NFSERR_CLIDINUSE && --clidinusedelay > 0));
		if (error) {
			NFSLOCKCLSTATE();
			nfsv4_unlock(&clp->nfsc_lock, 0);
			NFSUNLOCKCLSTATE();
			return (error);
		}
		clp->nfsc_flags |= NFSCLFLAGS_HASCLIENTID;
	}
	if (igotlock) {
		NFSLOCKCLSTATE();
		nfsv4_unlock(&clp->nfsc_lock, 1);
		NFSUNLOCKCLSTATE();
	}

	*clpp = clp;
	return (0);
}

/*
 * Get a reference to a clientid and return it, if valid.
 */
APPLESTATIC struct nfsclclient *
nfscl_findcl(struct nfsmount *nmp)
{
	struct nfsclclient *clp;

	clp = nmp->nm_clp;
	if (clp == NULL || !(clp->nfsc_flags & NFSCLFLAGS_HASCLIENTID))
		return (NULL);
	return (clp);
}

/*
 * Release the clientid structure. It may be locked or reference counted.
 */
static void
nfscl_clrelease(struct nfsclclient *clp)
{

	if (clp->nfsc_lock.nfslock_lock & NFSV4LOCK_LOCK)
		nfsv4_unlock(&clp->nfsc_lock, 0);
	else
		nfsv4_relref(&clp->nfsc_lock);
}

/*
 * External call for nfscl_clrelease.
 */
APPLESTATIC void
nfscl_clientrelease(struct nfsclclient *clp)
{

	NFSLOCKCLSTATE();
	if (clp->nfsc_lock.nfslock_lock & NFSV4LOCK_LOCK)
		nfsv4_unlock(&clp->nfsc_lock, 0);
	else
		nfsv4_relref(&clp->nfsc_lock);
	NFSUNLOCKCLSTATE();
}

/*
 * Called when wanting to lock a byte region.
 */
APPLESTATIC int
nfscl_getbytelock(vnode_t vp, u_int64_t off, u_int64_t len,
    short type, struct ucred *cred, NFSPROC_T *p, struct nfsclclient *rclp,
    int recovery, void *id, int flags, u_int8_t *rownp, u_int8_t *ropenownp,
    struct nfscllockowner **lpp, int *newonep, int *donelocallyp)
{
	struct nfscllockowner *lp;
	struct nfsclopen *op;
	struct nfsclclient *clp;
	struct nfscllockowner *nlp;
	struct nfscllock *nlop, *otherlop;
	struct nfscldeleg *dp = NULL, *ldp = NULL;
	struct nfscllockownerhead *lhp = NULL;
	struct nfsnode *np;
	u_int8_t own[NFSV4CL_LOCKNAMELEN], *ownp, openown[NFSV4CL_LOCKNAMELEN];
	u_int8_t *openownp;
	int error = 0, ret, donelocally = 0;
	u_int32_t mode;

	/* For Lock Ops, the open mode doesn't matter, so use 0 to match any. */
	mode = 0;
	np = VTONFS(vp);
	*lpp = NULL;
	lp = NULL;
	*newonep = 0;
	*donelocallyp = 0;

	/*
	 * Might need these, so MALLOC them now, to
	 * avoid a tsleep() in MALLOC later.
	 */
	nlp = malloc(
	    sizeof (struct nfscllockowner), M_NFSCLLOCKOWNER, M_WAITOK);
	otherlop = malloc(
	    sizeof (struct nfscllock), M_NFSCLLOCK, M_WAITOK);
	nlop = malloc(
	    sizeof (struct nfscllock), M_NFSCLLOCK, M_WAITOK);
	nlop->nfslo_type = type;
	nlop->nfslo_first = off;
	if (len == NFS64BITSSET) {
		nlop->nfslo_end = NFS64BITSSET;
	} else {
		nlop->nfslo_end = off + len;
		if (nlop->nfslo_end <= nlop->nfslo_first)
			error = NFSERR_INVAL;
	}

	if (!error) {
		if (recovery)
			clp = rclp;
		else
			error = nfscl_getcl(vnode_mount(vp), cred, p, 1, &clp);
	}
	if (error) {
		free(nlp, M_NFSCLLOCKOWNER);
		free(otherlop, M_NFSCLLOCK);
		free(nlop, M_NFSCLLOCK);
		return (error);
	}

	op = NULL;
	if (recovery) {
		ownp = rownp;
		openownp = ropenownp;
	} else {
		nfscl_filllockowner(id, own, flags);
		ownp = own;
		if (NFSHASONEOPENOWN(VFSTONFS(vnode_mount(vp))))
			nfscl_filllockowner(NULL, openown, F_POSIX);
		else
			nfscl_filllockowner(p->td_proc, openown, F_POSIX);
		openownp = openown;
	}
	if (!recovery) {
		NFSLOCKCLSTATE();
		/*
		 * First, search for a delegation. If one exists for this file,
		 * the lock can be done locally against it, so long as there
		 * isn't a local lock conflict.
		 */
		ldp = dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh,
		    np->n_fhp->nfh_len);
		/* Just sanity check for correct type of delegation */
		if (dp != NULL && ((dp->nfsdl_flags &
		    (NFSCLDL_RECALL | NFSCLDL_DELEGRET)) != 0 ||
		     (type == F_WRLCK &&
		      (dp->nfsdl_flags & NFSCLDL_WRITE) == 0)))
			dp = NULL;
	}
	if (dp != NULL) {
		/* Now, find an open and maybe a lockowner. */
		ret = nfscl_getopen(&dp->nfsdl_owner, np->n_fhp->nfh_fh,
		    np->n_fhp->nfh_len, openownp, ownp, mode, NULL, &op);
		if (ret)
			ret = nfscl_getopen(&clp->nfsc_owner,
			    np->n_fhp->nfh_fh, np->n_fhp->nfh_len, openownp,
			    ownp, mode, NULL, &op);
		if (!ret) {
			lhp = &dp->nfsdl_lock;
			TAILQ_REMOVE(&clp->nfsc_deleg, dp, nfsdl_list);
			TAILQ_INSERT_HEAD(&clp->nfsc_deleg, dp, nfsdl_list);
			dp->nfsdl_timestamp = NFSD_MONOSEC + 120;
			donelocally = 1;
		} else {
			dp = NULL;
		}
	}
	if (!donelocally) {
		/*
		 * Get the related Open and maybe lockowner.
		 */
		error = nfscl_getopen(&clp->nfsc_owner,
		    np->n_fhp->nfh_fh, np->n_fhp->nfh_len, openownp,
		    ownp, mode, &lp, &op);
		if (!error)
			lhp = &op->nfso_lock;
	}
	if (!error && !recovery)
		error = nfscl_localconflict(clp, np->n_fhp->nfh_fh,
		    np->n_fhp->nfh_len, nlop, ownp, ldp, NULL);
	if (error) {
		if (!recovery) {
			nfscl_clrelease(clp);
			NFSUNLOCKCLSTATE();
		}
		free(nlp, M_NFSCLLOCKOWNER);
		free(otherlop, M_NFSCLLOCK);
		free(nlop, M_NFSCLLOCK);
		return (error);
	}

	/*
	 * Ok, see if a lockowner exists and create one, as required.
	 */
	if (lp == NULL)
		LIST_FOREACH(lp, lhp, nfsl_list) {
			if (!NFSBCMP(lp->nfsl_owner, ownp, NFSV4CL_LOCKNAMELEN))
				break;
		}
	if (lp == NULL) {
		NFSBCOPY(ownp, nlp->nfsl_owner, NFSV4CL_LOCKNAMELEN);
		if (recovery)
			NFSBCOPY(ropenownp, nlp->nfsl_openowner,
			    NFSV4CL_LOCKNAMELEN);
		else
			NFSBCOPY(op->nfso_own->nfsow_owner, nlp->nfsl_openowner,
			    NFSV4CL_LOCKNAMELEN);
		nlp->nfsl_seqid = 0;
		nlp->nfsl_lockflags = flags;
		nlp->nfsl_inprog = NULL;
		nfscl_lockinit(&nlp->nfsl_rwlock);
		LIST_INIT(&nlp->nfsl_lock);
		if (donelocally) {
			nlp->nfsl_open = NULL;
			nfsstatsv1.cllocallockowners++;
		} else {
			nlp->nfsl_open = op;
			nfsstatsv1.cllockowners++;
		}
		LIST_INSERT_HEAD(lhp, nlp, nfsl_list);
		lp = nlp;
		nlp = NULL;
		*newonep = 1;
	}

	/*
	 * Now, update the byte ranges for locks.
	 */
	ret = nfscl_updatelock(lp, &nlop, &otherlop, donelocally);
	if (!ret)
		donelocally = 1;
	if (donelocally) {
		*donelocallyp = 1;
		if (!recovery)
			nfscl_clrelease(clp);
	} else {
		/*
		 * Serial modifications on the lock owner for multiple threads
		 * for the same process using a read/write lock.
		 */
		if (!recovery)
			nfscl_lockexcl(&lp->nfsl_rwlock, NFSCLSTATEMUTEXPTR);
	}
	if (!recovery)
		NFSUNLOCKCLSTATE();

	if (nlp)
		free(nlp, M_NFSCLLOCKOWNER);
	if (nlop)
		free(nlop, M_NFSCLLOCK);
	if (otherlop)
		free(otherlop, M_NFSCLLOCK);

	*lpp = lp;
	return (0);
}

/*
 * Called to unlock a byte range, for LockU.
 */
APPLESTATIC int
nfscl_relbytelock(vnode_t vp, u_int64_t off, u_int64_t len,
    __unused struct ucred *cred, NFSPROC_T *p, int callcnt,
    struct nfsclclient *clp, void *id, int flags,
    struct nfscllockowner **lpp, int *dorpcp)
{
	struct nfscllockowner *lp;
	struct nfsclowner *owp;
	struct nfsclopen *op;
	struct nfscllock *nlop, *other_lop = NULL;
	struct nfscldeleg *dp;
	struct nfsnode *np;
	u_int8_t own[NFSV4CL_LOCKNAMELEN];
	int ret = 0, fnd;

	np = VTONFS(vp);
	*lpp = NULL;
	*dorpcp = 0;

	/*
	 * Might need these, so MALLOC them now, to
	 * avoid a tsleep() in MALLOC later.
	 */
	nlop = malloc(
	    sizeof (struct nfscllock), M_NFSCLLOCK, M_WAITOK);
	nlop->nfslo_type = F_UNLCK;
	nlop->nfslo_first = off;
	if (len == NFS64BITSSET) {
		nlop->nfslo_end = NFS64BITSSET;
	} else {
		nlop->nfslo_end = off + len;
		if (nlop->nfslo_end <= nlop->nfslo_first) {
			free(nlop, M_NFSCLLOCK);
			return (NFSERR_INVAL);
		}
	}
	if (callcnt == 0) {
		other_lop = malloc(
		    sizeof (struct nfscllock), M_NFSCLLOCK, M_WAITOK);
		*other_lop = *nlop;
	}
	nfscl_filllockowner(id, own, flags);
	dp = NULL;
	NFSLOCKCLSTATE();
	if (callcnt == 0)
		dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh,
		    np->n_fhp->nfh_len);

	/*
	 * First, unlock any local regions on a delegation.
	 */
	if (dp != NULL) {
		/* Look for this lockowner. */
		LIST_FOREACH(lp, &dp->nfsdl_lock, nfsl_list) {
			if (!NFSBCMP(lp->nfsl_owner, own,
			    NFSV4CL_LOCKNAMELEN))
				break;
		}
		if (lp != NULL)
			/* Use other_lop, so nlop is still available */
			(void)nfscl_updatelock(lp, &other_lop, NULL, 1);
	}

	/*
	 * Now, find a matching open/lockowner that hasn't already been done,
	 * as marked by nfsl_inprog.
	 */
	lp = NULL;
	fnd = 0;
	LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
	    LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
		if (op->nfso_fhlen == np->n_fhp->nfh_len &&
		    !NFSBCMP(op->nfso_fh, np->n_fhp->nfh_fh, op->nfso_fhlen)) {
		    LIST_FOREACH(lp, &op->nfso_lock, nfsl_list) {
			if (lp->nfsl_inprog == NULL &&
			    !NFSBCMP(lp->nfsl_owner, own,
			     NFSV4CL_LOCKNAMELEN)) {
				fnd = 1;
				break;
			}
		    }
		    if (fnd)
			break;
		}
	    }
	    if (fnd)
		break;
	}

	if (lp != NULL) {
		ret = nfscl_updatelock(lp, &nlop, NULL, 0);
		if (ret)
			*dorpcp = 1;
		/*
		 * Serial modifications on the lock owner for multiple
		 * threads for the same process using a read/write lock.
		 */
		lp->nfsl_inprog = p;
		nfscl_lockexcl(&lp->nfsl_rwlock, NFSCLSTATEMUTEXPTR);
		*lpp = lp;
	}
	NFSUNLOCKCLSTATE();
	if (nlop)
		free(nlop, M_NFSCLLOCK);
	if (other_lop)
		free(other_lop, M_NFSCLLOCK);
	return (0);
}

/*
 * Release all lockowners marked in progess for this process and file.
 */
APPLESTATIC void
nfscl_releasealllocks(struct nfsclclient *clp, vnode_t vp, NFSPROC_T *p,
    void *id, int flags)
{
	struct nfsclowner *owp;
	struct nfsclopen *op;
	struct nfscllockowner *lp;
	struct nfsnode *np;
	u_int8_t own[NFSV4CL_LOCKNAMELEN];

	np = VTONFS(vp);
	nfscl_filllockowner(id, own, flags);
	NFSLOCKCLSTATE();
	LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
	    LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
		if (op->nfso_fhlen == np->n_fhp->nfh_len &&
		    !NFSBCMP(op->nfso_fh, np->n_fhp->nfh_fh, op->nfso_fhlen)) {
		    LIST_FOREACH(lp, &op->nfso_lock, nfsl_list) {
			if (lp->nfsl_inprog == p &&
			    !NFSBCMP(lp->nfsl_owner, own,
			    NFSV4CL_LOCKNAMELEN)) {
			    lp->nfsl_inprog = NULL;
			    nfscl_lockunlock(&lp->nfsl_rwlock);
			}
		    }
		}
	    }
	}
	nfscl_clrelease(clp);
	NFSUNLOCKCLSTATE();
}

/*
 * Called to find out if any bytes within the byte range specified are
 * write locked by the calling process. Used to determine if flushing
 * is required before a LockU.
 * If in doubt, return 1, so the flush will occur.
 */
APPLESTATIC int
nfscl_checkwritelocked(vnode_t vp, struct flock *fl,
    struct ucred *cred, NFSPROC_T *p, void *id, int flags)
{
	struct nfsclowner *owp;
	struct nfscllockowner *lp;
	struct nfsclopen *op;
	struct nfsclclient *clp;
	struct nfscllock *lop;
	struct nfscldeleg *dp;
	struct nfsnode *np;
	u_int64_t off, end;
	u_int8_t own[NFSV4CL_LOCKNAMELEN];
	int error = 0;

	np = VTONFS(vp);
	switch (fl->l_whence) {
	case SEEK_SET:
	case SEEK_CUR:
		/*
		 * Caller is responsible for adding any necessary offset
		 * when SEEK_CUR is used.
		 */
		off = fl->l_start;
		break;
	case SEEK_END:
		off = np->n_size + fl->l_start;
		break;
	default:
		return (1);
	}
	if (fl->l_len != 0) {
		end = off + fl->l_len;
		if (end < off)
			return (1);
	} else {
		end = NFS64BITSSET;
	}

	error = nfscl_getcl(vnode_mount(vp), cred, p, 1, &clp);
	if (error)
		return (1);
	nfscl_filllockowner(id, own, flags);
	NFSLOCKCLSTATE();

	/*
	 * First check the delegation locks.
	 */
	dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len);
	if (dp != NULL) {
		LIST_FOREACH(lp, &dp->nfsdl_lock, nfsl_list) {
			if (!NFSBCMP(lp->nfsl_owner, own,
			    NFSV4CL_LOCKNAMELEN))
				break;
		}
		if (lp != NULL) {
			LIST_FOREACH(lop, &lp->nfsl_lock, nfslo_list) {
				if (lop->nfslo_first >= end)
					break;
				if (lop->nfslo_end <= off)
					continue;
				if (lop->nfslo_type == F_WRLCK) {
					nfscl_clrelease(clp);
					NFSUNLOCKCLSTATE();
					return (1);
				}
			}
		}
	}

	/*
	 * Now, check state against the server.
	 */
	LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
	    LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
		if (op->nfso_fhlen == np->n_fhp->nfh_len &&
		    !NFSBCMP(op->nfso_fh, np->n_fhp->nfh_fh, op->nfso_fhlen)) {
		    LIST_FOREACH(lp, &op->nfso_lock, nfsl_list) {
			if (!NFSBCMP(lp->nfsl_owner, own,
			    NFSV4CL_LOCKNAMELEN))
			    break;
		    }
		    if (lp != NULL) {
			LIST_FOREACH(lop, &lp->nfsl_lock, nfslo_list) {
			    if (lop->nfslo_first >= end)
				break;
			    if (lop->nfslo_end <= off)
				continue;
			    if (lop->nfslo_type == F_WRLCK) {
				nfscl_clrelease(clp);
				NFSUNLOCKCLSTATE();
				return (1);
			    }
			}
		    }
		}
	    }
	}
	nfscl_clrelease(clp);
	NFSUNLOCKCLSTATE();
	return (0);
}

/*
 * Release a byte range lock owner structure.
 */
APPLESTATIC void
nfscl_lockrelease(struct nfscllockowner *lp, int error, int candelete)
{
	struct nfsclclient *clp;

	if (lp == NULL)
		return;
	NFSLOCKCLSTATE();
	clp = lp->nfsl_open->nfso_own->nfsow_clp;
	if (error != 0 && candelete &&
	    (lp->nfsl_rwlock.nfslock_lock & NFSV4LOCK_WANTED) == 0)
		nfscl_freelockowner(lp, 0);
	else
		nfscl_lockunlock(&lp->nfsl_rwlock);
	nfscl_clrelease(clp);
	NFSUNLOCKCLSTATE();
}

/*
 * Free up an open structure and any associated byte range lock structures.
 */
APPLESTATIC void
nfscl_freeopen(struct nfsclopen *op, int local)
{

	LIST_REMOVE(op, nfso_list);
	nfscl_freealllocks(&op->nfso_lock, local);
	free(op, M_NFSCLOPEN);
	if (local)
		nfsstatsv1.cllocalopens--;
	else
		nfsstatsv1.clopens--;
}

/*
 * Free up all lock owners and associated locks.
 */
static void
nfscl_freealllocks(struct nfscllockownerhead *lhp, int local)
{
	struct nfscllockowner *lp, *nlp;

	LIST_FOREACH_SAFE(lp, lhp, nfsl_list, nlp) {
		if ((lp->nfsl_rwlock.nfslock_lock & NFSV4LOCK_WANTED))
			panic("nfscllckw");
		nfscl_freelockowner(lp, local);
	}
}

/*
 * Called for an Open when NFSERR_EXPIRED is received from the server.
 * If there are no byte range locks nor a Share Deny lost, try to do a
 * fresh Open. Otherwise, free the open.
 */
static int
nfscl_expireopen(struct nfsclclient *clp, struct nfsclopen *op,
    struct nfsmount *nmp, struct ucred *cred, NFSPROC_T *p)
{
	struct nfscllockowner *lp;
	struct nfscldeleg *dp;
	int mustdelete = 0, error;

	/*
	 * Look for any byte range lock(s).
	 */
	LIST_FOREACH(lp, &op->nfso_lock, nfsl_list) {
		if (!LIST_EMPTY(&lp->nfsl_lock)) {
			mustdelete = 1;
			break;
		}
	}

	/*
	 * If no byte range lock(s) nor a Share deny, try to re-open.
	 */
	if (!mustdelete && (op->nfso_mode & NFSLCK_DENYBITS) == 0) {
		newnfs_copycred(&op->nfso_cred, cred);
		dp = NULL;
		error = nfsrpc_reopen(nmp, op->nfso_fh,
		    op->nfso_fhlen, op->nfso_mode, op, &dp, cred, p);
		if (error) {
			mustdelete = 1;
			if (dp != NULL) {
				free(dp, M_NFSCLDELEG);
				dp = NULL;
			}
		}
		if (dp != NULL)
			nfscl_deleg(nmp->nm_mountp, clp, op->nfso_fh,
			    op->nfso_fhlen, cred, p, &dp);
	}

	/*
	 * If a byte range lock or Share deny or couldn't re-open, free it.
	 */
	if (mustdelete)
		nfscl_freeopen(op, 0);
	return (mustdelete);
}

/*
 * Free up an open owner structure.
 */
static void
nfscl_freeopenowner(struct nfsclowner *owp, int local)
{

	LIST_REMOVE(owp, nfsow_list);
	free(owp, M_NFSCLOWNER);
	if (local)
		nfsstatsv1.cllocalopenowners--;
	else
		nfsstatsv1.clopenowners--;
}

/*
 * Free up a byte range lock owner structure.
 */
APPLESTATIC void
nfscl_freelockowner(struct nfscllockowner *lp, int local)
{
	struct nfscllock *lop, *nlop;

	LIST_REMOVE(lp, nfsl_list);
	LIST_FOREACH_SAFE(lop, &lp->nfsl_lock, nfslo_list, nlop) {
		nfscl_freelock(lop, local);
	}
	free(lp, M_NFSCLLOCKOWNER);
	if (local)
		nfsstatsv1.cllocallockowners--;
	else
		nfsstatsv1.cllockowners--;
}

/*
 * Free up a byte range lock structure.
 */
APPLESTATIC void
nfscl_freelock(struct nfscllock *lop, int local)
{

	LIST_REMOVE(lop, nfslo_list);
	free(lop, M_NFSCLLOCK);
	if (local)
		nfsstatsv1.cllocallocks--;
	else
		nfsstatsv1.cllocks--;
}

/*
 * Clean out the state related to a delegation.
 */
static void
nfscl_cleandeleg(struct nfscldeleg *dp)
{
	struct nfsclowner *owp, *nowp;
	struct nfsclopen *op;

	LIST_FOREACH_SAFE(owp, &dp->nfsdl_owner, nfsow_list, nowp) {
		op = LIST_FIRST(&owp->nfsow_open);
		if (op != NULL) {
			if (LIST_NEXT(op, nfso_list) != NULL)
				panic("nfscleandel");
			nfscl_freeopen(op, 1);
		}
		nfscl_freeopenowner(owp, 1);
	}
	nfscl_freealllocks(&dp->nfsdl_lock, 1);
}

/*
 * Free a delegation.
 */
static void
nfscl_freedeleg(struct nfscldeleghead *hdp, struct nfscldeleg *dp)
{

	TAILQ_REMOVE(hdp, dp, nfsdl_list);
	LIST_REMOVE(dp, nfsdl_hash);
	free(dp, M_NFSCLDELEG);
	nfsstatsv1.cldelegates--;
	nfscl_delegcnt--;
}

/*
 * Free up all state related to this client structure.
 */
static void
nfscl_cleanclient(struct nfsclclient *clp)
{
	struct nfsclowner *owp, *nowp;
	struct nfsclopen *op, *nop;
	struct nfscllayout *lyp, *nlyp;
	struct nfscldevinfo *dip, *ndip;

	TAILQ_FOREACH_SAFE(lyp, &clp->nfsc_layout, nfsly_list, nlyp)
		nfscl_freelayout(lyp);

	LIST_FOREACH_SAFE(dip, &clp->nfsc_devinfo, nfsdi_list, ndip)
		nfscl_freedevinfo(dip);

	/* Now, all the OpenOwners, etc. */
	LIST_FOREACH_SAFE(owp, &clp->nfsc_owner, nfsow_list, nowp) {
		LIST_FOREACH_SAFE(op, &owp->nfsow_open, nfso_list, nop) {
			nfscl_freeopen(op, 0);
		}
		nfscl_freeopenowner(owp, 0);
	}
}

/*
 * Called when an NFSERR_EXPIRED is received from the server.
 */
static void
nfscl_expireclient(struct nfsclclient *clp, struct nfsmount *nmp,
    struct ucred *cred, NFSPROC_T *p)
{
	struct nfsclowner *owp, *nowp, *towp;
	struct nfsclopen *op, *nop, *top;
	struct nfscldeleg *dp, *ndp;
	int ret, printed = 0;

	/*
	 * First, merge locally issued Opens into the list for the server.
	 */
	dp = TAILQ_FIRST(&clp->nfsc_deleg);
	while (dp != NULL) {
	    ndp = TAILQ_NEXT(dp, nfsdl_list);
	    owp = LIST_FIRST(&dp->nfsdl_owner);
	    while (owp != NULL) {
		nowp = LIST_NEXT(owp, nfsow_list);
		op = LIST_FIRST(&owp->nfsow_open);
		if (op != NULL) {
		    if (LIST_NEXT(op, nfso_list) != NULL)
			panic("nfsclexp");
		    LIST_FOREACH(towp, &clp->nfsc_owner, nfsow_list) {
			if (!NFSBCMP(towp->nfsow_owner, owp->nfsow_owner,
			    NFSV4CL_LOCKNAMELEN))
			    break;
		    }
		    if (towp != NULL) {
			/* Merge opens in */
			LIST_FOREACH(top, &towp->nfsow_open, nfso_list) {
			    if (top->nfso_fhlen == op->nfso_fhlen &&
				!NFSBCMP(top->nfso_fh, op->nfso_fh,
				 op->nfso_fhlen)) {
				top->nfso_mode |= op->nfso_mode;
				top->nfso_opencnt += op->nfso_opencnt;
				break;
			    }
			}
			if (top == NULL) {
			    /* Just add the open to the owner list */
			    LIST_REMOVE(op, nfso_list);
			    op->nfso_own = towp;
			    LIST_INSERT_HEAD(&towp->nfsow_open, op, nfso_list);
			    nfsstatsv1.cllocalopens--;
			    nfsstatsv1.clopens++;
			}
		    } else {
			/* Just add the openowner to the client list */
			LIST_REMOVE(owp, nfsow_list);
			owp->nfsow_clp = clp;
			LIST_INSERT_HEAD(&clp->nfsc_owner, owp, nfsow_list);
			nfsstatsv1.cllocalopenowners--;
			nfsstatsv1.clopenowners++;
			nfsstatsv1.cllocalopens--;
			nfsstatsv1.clopens++;
		    }
		}
		owp = nowp;
	    }
	    if (!printed && !LIST_EMPTY(&dp->nfsdl_lock)) {
		printed = 1;
		printf("nfsv4 expired locks lost\n");
	    }
	    nfscl_cleandeleg(dp);
	    nfscl_freedeleg(&clp->nfsc_deleg, dp);
	    dp = ndp;
	}
	if (!TAILQ_EMPTY(&clp->nfsc_deleg))
	    panic("nfsclexp");

	/*
	 * Now, try and reopen against the server.
	 */
	LIST_FOREACH_SAFE(owp, &clp->nfsc_owner, nfsow_list, nowp) {
		owp->nfsow_seqid = 0;
		LIST_FOREACH_SAFE(op, &owp->nfsow_open, nfso_list, nop) {
			ret = nfscl_expireopen(clp, op, nmp, cred, p);
			if (ret && !printed) {
				printed = 1;
				printf("nfsv4 expired locks lost\n");
			}
		}
		if (LIST_EMPTY(&owp->nfsow_open))
			nfscl_freeopenowner(owp, 0);
	}
}

/*
 * This function must be called after the process represented by "own" has
 * exited. Must be called with CLSTATE lock held.
 */
static void
nfscl_cleanup_common(struct nfsclclient *clp, u_int8_t *own)
{
	struct nfsclowner *owp, *nowp;
	struct nfscllockowner *lp, *nlp;
	struct nfscldeleg *dp;

	/* First, get rid of local locks on delegations. */
	TAILQ_FOREACH(dp, &clp->nfsc_deleg, nfsdl_list) {
		LIST_FOREACH_SAFE(lp, &dp->nfsdl_lock, nfsl_list, nlp) {
		    if (!NFSBCMP(lp->nfsl_owner, own, NFSV4CL_LOCKNAMELEN)) {
			if ((lp->nfsl_rwlock.nfslock_lock & NFSV4LOCK_WANTED))
			    panic("nfscllckw");
			nfscl_freelockowner(lp, 1);
		    }
		}
	}
	owp = LIST_FIRST(&clp->nfsc_owner);
	while (owp != NULL) {
		nowp = LIST_NEXT(owp, nfsow_list);
		if (!NFSBCMP(owp->nfsow_owner, own,
		    NFSV4CL_LOCKNAMELEN)) {
			/*
			 * If there are children that haven't closed the
			 * file descriptors yet, the opens will still be
			 * here. For that case, let the renew thread clear
			 * out the OpenOwner later.
			 */
			if (LIST_EMPTY(&owp->nfsow_open))
				nfscl_freeopenowner(owp, 0);
			else
				owp->nfsow_defunct = 1;
		}
		owp = nowp;
	}
}

/*
 * Find open/lock owners for processes that have exited.
 */
static void
nfscl_cleanupkext(struct nfsclclient *clp, struct nfscllockownerfhhead *lhp)
{
	struct nfsclowner *owp, *nowp;
	struct nfsclopen *op;
	struct nfscllockowner *lp, *nlp;
	struct nfscldeleg *dp;

	NFSPROCLISTLOCK();
	NFSLOCKCLSTATE();
	LIST_FOREACH_SAFE(owp, &clp->nfsc_owner, nfsow_list, nowp) {
		LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
			LIST_FOREACH_SAFE(lp, &op->nfso_lock, nfsl_list, nlp) {
				if (LIST_EMPTY(&lp->nfsl_lock))
					nfscl_emptylockowner(lp, lhp);
			}
		}
		if (nfscl_procdoesntexist(owp->nfsow_owner))
			nfscl_cleanup_common(clp, owp->nfsow_owner);
	}

	/*
	 * For the single open_owner case, these lock owners need to be
	 * checked to see if they still exist separately.
	 * This is because nfscl_procdoesntexist() never returns true for
	 * the single open_owner so that the above doesn't ever call
	 * nfscl_cleanup_common().
	 */
	TAILQ_FOREACH(dp, &clp->nfsc_deleg, nfsdl_list) {
		LIST_FOREACH_SAFE(lp, &dp->nfsdl_lock, nfsl_list, nlp) {
			if (nfscl_procdoesntexist(lp->nfsl_owner))
				nfscl_cleanup_common(clp, lp->nfsl_owner);
		}
	}
	NFSUNLOCKCLSTATE();
	NFSPROCLISTUNLOCK();
}

/*
 * Take the empty lock owner and move it to the local lhp list if the
 * associated process no longer exists.
 */
static void
nfscl_emptylockowner(struct nfscllockowner *lp,
    struct nfscllockownerfhhead *lhp)
{
	struct nfscllockownerfh *lfhp, *mylfhp;
	struct nfscllockowner *nlp;
	int fnd_it;

	/* If not a Posix lock owner, just return. */
	if ((lp->nfsl_lockflags & F_POSIX) == 0)
		return;

	fnd_it = 0;
	mylfhp = NULL;
	/*
	 * First, search to see if this lock owner is already in the list.
	 * If it is, then the associated process no longer exists.
	 */
	SLIST_FOREACH(lfhp, lhp, nfslfh_list) {
		if (lfhp->nfslfh_len == lp->nfsl_open->nfso_fhlen &&
		    !NFSBCMP(lfhp->nfslfh_fh, lp->nfsl_open->nfso_fh,
		    lfhp->nfslfh_len))
			mylfhp = lfhp;
		LIST_FOREACH(nlp, &lfhp->nfslfh_lock, nfsl_list)
			if (!NFSBCMP(nlp->nfsl_owner, lp->nfsl_owner,
			    NFSV4CL_LOCKNAMELEN))
				fnd_it = 1;
	}
	/* If not found, check if process still exists. */
	if (fnd_it == 0 && nfscl_procdoesntexist(lp->nfsl_owner) == 0)
		return;

	/* Move the lock owner over to the local list. */
	if (mylfhp == NULL) {
		mylfhp = malloc(sizeof(struct nfscllockownerfh), M_TEMP,
		    M_NOWAIT);
		if (mylfhp == NULL)
			return;
		mylfhp->nfslfh_len = lp->nfsl_open->nfso_fhlen;
		NFSBCOPY(lp->nfsl_open->nfso_fh, mylfhp->nfslfh_fh,
		    mylfhp->nfslfh_len);
		LIST_INIT(&mylfhp->nfslfh_lock);
		SLIST_INSERT_HEAD(lhp, mylfhp, nfslfh_list);
	}
	LIST_REMOVE(lp, nfsl_list);
	LIST_INSERT_HEAD(&mylfhp->nfslfh_lock, lp, nfsl_list);
}

static int	fake_global;	/* Used to force visibility of MNTK_UNMOUNTF */
/*
 * Called from nfs umount to free up the clientid.
 */
APPLESTATIC void
nfscl_umount(struct nfsmount *nmp, NFSPROC_T *p)
{
	struct nfsclclient *clp;
	struct ucred *cred;
	int igotlock;

	/*
	 * For the case that matters, this is the thread that set
	 * MNTK_UNMOUNTF, so it will see it set. The code that follows is
	 * done to ensure that any thread executing nfscl_getcl() after
	 * this time, will see MNTK_UNMOUNTF set. nfscl_getcl() uses the
	 * mutex for NFSLOCKCLSTATE(), so it is "m" for the following
	 * explanation, courtesy of Alan Cox.
	 * What follows is a snippet from Alan Cox's email at:
	 * https://docs.FreeBSD.org/cgi/mid.cgi?BANLkTikR3d65zPHo9==08ZfJ2vmqZucEvw
	 * 
	 * 1. Set MNTK_UNMOUNTF
	 * 2. Acquire a standard FreeBSD mutex "m".
	 * 3. Update some data structures.
	 * 4. Release mutex "m".
	 * 
	 * Then, other threads that acquire "m" after step 4 has occurred will
	 * see MNTK_UNMOUNTF as set.  But, other threads that beat thread X to
	 * step 2 may or may not see MNTK_UNMOUNTF as set.
	 */
	NFSLOCKCLSTATE();
	if ((nmp->nm_mountp->mnt_kern_flag & MNTK_UNMOUNTF) != 0) {
		fake_global++;
		NFSUNLOCKCLSTATE();
		NFSLOCKCLSTATE();
	}

	clp = nmp->nm_clp;
	if (clp != NULL) {
		if ((clp->nfsc_flags & NFSCLFLAGS_INITED) == 0)
			panic("nfscl umount");
	
		/*
		 * First, handshake with the nfscl renew thread, to terminate
		 * it.
		 */
		clp->nfsc_flags |= NFSCLFLAGS_UMOUNT;
		while (clp->nfsc_flags & NFSCLFLAGS_HASTHREAD)
			(void)mtx_sleep(clp, NFSCLSTATEMUTEXPTR, PWAIT,
			    "nfsclumnt", hz);
	
		/*
		 * Now, get the exclusive lock on the client state, so
		 * that no uses of the state are still in progress.
		 */
		do {
			igotlock = nfsv4_lock(&clp->nfsc_lock, 1, NULL,
			    NFSCLSTATEMUTEXPTR, NULL);
		} while (!igotlock);
		NFSUNLOCKCLSTATE();
	
		/*
		 * Free up all the state. It will expire on the server, but
		 * maybe we should do a SetClientId/SetClientIdConfirm so
		 * the server throws it away?
		 */
		LIST_REMOVE(clp, nfsc_list);
		nfscl_delegreturnall(clp, p);
		cred = newnfs_getcred();
		if (NFSHASNFSV4N(nmp)) {
			(void)nfsrpc_destroysession(nmp, clp, cred, p);
			(void)nfsrpc_destroyclient(nmp, clp, cred, p);
		} else
			(void)nfsrpc_setclient(nmp, clp, 0, cred, p);
		nfscl_cleanclient(clp);
		nmp->nm_clp = NULL;
		NFSFREECRED(cred);
		free(clp, M_NFSCLCLIENT);
	} else
		NFSUNLOCKCLSTATE();
}

/*
 * This function is called when a server replies with NFSERR_STALECLIENTID
 * NFSERR_STALESTATEID or NFSERR_BADSESSION. It traverses the clientid lists,
 * doing Opens and Locks with reclaim. If these fail, it deletes the
 * corresponding state.
 */
static void
nfscl_recover(struct nfsclclient *clp, struct ucred *cred, NFSPROC_T *p)
{
	struct nfsclowner *owp, *nowp;
	struct nfsclopen *op, *nop;
	struct nfscllockowner *lp, *nlp;
	struct nfscllock *lop, *nlop;
	struct nfscldeleg *dp, *ndp, *tdp;
	struct nfsmount *nmp;
	struct ucred *tcred;
	struct nfsclopenhead extra_open;
	struct nfscldeleghead extra_deleg;
	struct nfsreq *rep;
	u_int64_t len;
	u_int32_t delegtype = NFSV4OPEN_DELEGATEWRITE, mode;
	int i, igotlock = 0, error, trycnt, firstlock;
	struct nfscllayout *lyp, *nlyp;

	/*
	 * First, lock the client structure, so everyone else will
	 * block when trying to use state.
	 */
	NFSLOCKCLSTATE();
	clp->nfsc_flags |= NFSCLFLAGS_RECVRINPROG;
	do {
		igotlock = nfsv4_lock(&clp->nfsc_lock, 1, NULL,
		    NFSCLSTATEMUTEXPTR, NULL);
	} while (!igotlock);
	NFSUNLOCKCLSTATE();

	nmp = clp->nfsc_nmp;
	if (nmp == NULL)
		panic("nfscl recover");

	/*
	 * For now, just get rid of all layouts. There may be a need
	 * to do LayoutCommit Ops with reclaim == true later.
	 */
	TAILQ_FOREACH_SAFE(lyp, &clp->nfsc_layout, nfsly_list, nlyp)
		nfscl_freelayout(lyp);
	TAILQ_INIT(&clp->nfsc_layout);
	for (i = 0; i < NFSCLLAYOUTHASHSIZE; i++)
		LIST_INIT(&clp->nfsc_layouthash[i]);

	trycnt = 5;
	do {
		error = nfsrpc_setclient(nmp, clp, 1, cred, p);
	} while ((error == NFSERR_STALECLIENTID ||
	     error == NFSERR_BADSESSION ||
	     error == NFSERR_STALEDONTRECOVER) && --trycnt > 0);
	if (error) {
		NFSLOCKCLSTATE();
		clp->nfsc_flags &= ~(NFSCLFLAGS_RECOVER |
		    NFSCLFLAGS_RECVRINPROG);
		wakeup(&clp->nfsc_flags);
		nfsv4_unlock(&clp->nfsc_lock, 0);
		NFSUNLOCKCLSTATE();
		return;
	}
	clp->nfsc_flags |= NFSCLFLAGS_HASCLIENTID;
	clp->nfsc_flags &= ~NFSCLFLAGS_RECOVER;

	/*
	 * Mark requests already queued on the server, so that they don't
	 * initiate another recovery cycle. Any requests already in the
	 * queue that handle state information will have the old stale
	 * clientid/stateid and will get a NFSERR_STALESTATEID,
	 * NFSERR_STALECLIENTID or NFSERR_BADSESSION reply from the server.
	 * This will be translated to NFSERR_STALEDONTRECOVER when
	 * R_DONTRECOVER is set.
	 */
	NFSLOCKREQ();
	TAILQ_FOREACH(rep, &nfsd_reqq, r_chain) {
		if (rep->r_nmp == nmp)
			rep->r_flags |= R_DONTRECOVER;
	}
	NFSUNLOCKREQ();

	/*
	 * Now, mark all delegations "need reclaim".
	 */
	TAILQ_FOREACH(dp, &clp->nfsc_deleg, nfsdl_list)
		dp->nfsdl_flags |= NFSCLDL_NEEDRECLAIM;

	TAILQ_INIT(&extra_deleg);
	LIST_INIT(&extra_open);
	/*
	 * Now traverse the state lists, doing Open and Lock Reclaims.
	 */
	tcred = newnfs_getcred();
	owp = LIST_FIRST(&clp->nfsc_owner);
	while (owp != NULL) {
	    nowp = LIST_NEXT(owp, nfsow_list);
	    owp->nfsow_seqid = 0;
	    op = LIST_FIRST(&owp->nfsow_open);
	    while (op != NULL) {
		nop = LIST_NEXT(op, nfso_list);
		if (error != NFSERR_NOGRACE && error != NFSERR_BADSESSION) {
		    /* Search for a delegation to reclaim with the open */
		    TAILQ_FOREACH(dp, &clp->nfsc_deleg, nfsdl_list) {
			if (!(dp->nfsdl_flags & NFSCLDL_NEEDRECLAIM))
			    continue;
			if ((dp->nfsdl_flags & NFSCLDL_WRITE)) {
			    mode = NFSV4OPEN_ACCESSWRITE;
			    delegtype = NFSV4OPEN_DELEGATEWRITE;
			} else {
			    mode = NFSV4OPEN_ACCESSREAD;
			    delegtype = NFSV4OPEN_DELEGATEREAD;
			}
			if ((op->nfso_mode & mode) == mode &&
			    op->nfso_fhlen == dp->nfsdl_fhlen &&
			    !NFSBCMP(op->nfso_fh, dp->nfsdl_fh, op->nfso_fhlen))
			    break;
		    }
		    ndp = dp;
		    if (dp == NULL)
			delegtype = NFSV4OPEN_DELEGATENONE;
		    newnfs_copycred(&op->nfso_cred, tcred);
		    error = nfscl_tryopen(nmp, NULL, op->nfso_fh,
			op->nfso_fhlen, op->nfso_fh, op->nfso_fhlen,
			op->nfso_mode, op, NULL, 0, &ndp, 1, delegtype,
			tcred, p);
		    if (!error) {
			/* Handle any replied delegation */
			if (ndp != NULL && ((ndp->nfsdl_flags & NFSCLDL_WRITE)
			    || NFSMNT_RDONLY(nmp->nm_mountp))) {
			    if ((ndp->nfsdl_flags & NFSCLDL_WRITE))
				mode = NFSV4OPEN_ACCESSWRITE;
			    else
				mode = NFSV4OPEN_ACCESSREAD;
			    TAILQ_FOREACH(dp, &clp->nfsc_deleg, nfsdl_list) {
				if (!(dp->nfsdl_flags & NFSCLDL_NEEDRECLAIM))
				    continue;
				if ((op->nfso_mode & mode) == mode &&
				    op->nfso_fhlen == dp->nfsdl_fhlen &&
				    !NFSBCMP(op->nfso_fh, dp->nfsdl_fh,
				    op->nfso_fhlen)) {
				    dp->nfsdl_stateid = ndp->nfsdl_stateid;
				    dp->nfsdl_sizelimit = ndp->nfsdl_sizelimit;
				    dp->nfsdl_ace = ndp->nfsdl_ace;
				    dp->nfsdl_change = ndp->nfsdl_change;
				    dp->nfsdl_flags &= ~NFSCLDL_NEEDRECLAIM;
				    if ((ndp->nfsdl_flags & NFSCLDL_RECALL))
					dp->nfsdl_flags |= NFSCLDL_RECALL;
				    free(ndp, M_NFSCLDELEG);
				    ndp = NULL;
				    break;
				}
			    }
			}
			if (ndp != NULL)
			    TAILQ_INSERT_HEAD(&extra_deleg, ndp, nfsdl_list);

			/* and reclaim all byte range locks */
			lp = LIST_FIRST(&op->nfso_lock);
			while (lp != NULL) {
			    nlp = LIST_NEXT(lp, nfsl_list);
			    lp->nfsl_seqid = 0;
			    firstlock = 1;
			    lop = LIST_FIRST(&lp->nfsl_lock);
			    while (lop != NULL) {
				nlop = LIST_NEXT(lop, nfslo_list);
				if (lop->nfslo_end == NFS64BITSSET)
				    len = NFS64BITSSET;
				else
				    len = lop->nfslo_end - lop->nfslo_first;
				error = nfscl_trylock(nmp, NULL,
				    op->nfso_fh, op->nfso_fhlen, lp,
				    firstlock, 1, lop->nfslo_first, len,
				    lop->nfslo_type, tcred, p);
				if (error != 0)
				    nfscl_freelock(lop, 0);
				else
				    firstlock = 0;
				lop = nlop;
			    }
			    /* If no locks, but a lockowner, just delete it. */
			    if (LIST_EMPTY(&lp->nfsl_lock))
				nfscl_freelockowner(lp, 0);
			    lp = nlp;
			}
		    }
		}
		if (error != 0 && error != NFSERR_BADSESSION)
		    nfscl_freeopen(op, 0);
		op = nop;
	    }
	    owp = nowp;
	}

	/*
	 * Now, try and get any delegations not yet reclaimed by cobbling
	 * to-gether an appropriate open.
	 */
	nowp = NULL;
	dp = TAILQ_FIRST(&clp->nfsc_deleg);
	while (dp != NULL) {
	    ndp = TAILQ_NEXT(dp, nfsdl_list);
	    if ((dp->nfsdl_flags & NFSCLDL_NEEDRECLAIM)) {
		if (nowp == NULL) {
		    nowp = malloc(
			sizeof (struct nfsclowner), M_NFSCLOWNER, M_WAITOK);
		    /*
		     * Name must be as long an largest possible
		     * NFSV4CL_LOCKNAMELEN. 12 for now.
		     */
		    NFSBCOPY("RECLAIMDELEG", nowp->nfsow_owner,
			NFSV4CL_LOCKNAMELEN);
		    LIST_INIT(&nowp->nfsow_open);
		    nowp->nfsow_clp = clp;
		    nowp->nfsow_seqid = 0;
		    nowp->nfsow_defunct = 0;
		    nfscl_lockinit(&nowp->nfsow_rwlock);
		}
		nop = NULL;
		if (error != NFSERR_NOGRACE && error != NFSERR_BADSESSION) {
		    nop = malloc(sizeof (struct nfsclopen) +
			dp->nfsdl_fhlen - 1, M_NFSCLOPEN, M_WAITOK);
		    nop->nfso_own = nowp;
		    if ((dp->nfsdl_flags & NFSCLDL_WRITE)) {
			nop->nfso_mode = NFSV4OPEN_ACCESSWRITE;
			delegtype = NFSV4OPEN_DELEGATEWRITE;
		    } else {
			nop->nfso_mode = NFSV4OPEN_ACCESSREAD;
			delegtype = NFSV4OPEN_DELEGATEREAD;
		    }
		    nop->nfso_opencnt = 0;
		    nop->nfso_posixlock = 1;
		    nop->nfso_fhlen = dp->nfsdl_fhlen;
		    NFSBCOPY(dp->nfsdl_fh, nop->nfso_fh, dp->nfsdl_fhlen);
		    LIST_INIT(&nop->nfso_lock);
		    nop->nfso_stateid.seqid = 0;
		    nop->nfso_stateid.other[0] = 0;
		    nop->nfso_stateid.other[1] = 0;
		    nop->nfso_stateid.other[2] = 0;
		    newnfs_copycred(&dp->nfsdl_cred, tcred);
		    newnfs_copyincred(tcred, &nop->nfso_cred);
		    tdp = NULL;
		    error = nfscl_tryopen(nmp, NULL, nop->nfso_fh,
			nop->nfso_fhlen, nop->nfso_fh, nop->nfso_fhlen,
			nop->nfso_mode, nop, NULL, 0, &tdp, 1,
			delegtype, tcred, p);
		    if (tdp != NULL) {
			if ((tdp->nfsdl_flags & NFSCLDL_WRITE))
			    mode = NFSV4OPEN_ACCESSWRITE;
			else
			    mode = NFSV4OPEN_ACCESSREAD;
			if ((nop->nfso_mode & mode) == mode &&
			    nop->nfso_fhlen == tdp->nfsdl_fhlen &&
			    !NFSBCMP(nop->nfso_fh, tdp->nfsdl_fh,
			    nop->nfso_fhlen)) {
			    dp->nfsdl_stateid = tdp->nfsdl_stateid;
			    dp->nfsdl_sizelimit = tdp->nfsdl_sizelimit;
			    dp->nfsdl_ace = tdp->nfsdl_ace;
			    dp->nfsdl_change = tdp->nfsdl_change;
			    dp->nfsdl_flags &= ~NFSCLDL_NEEDRECLAIM;
			    if ((tdp->nfsdl_flags & NFSCLDL_RECALL))
				dp->nfsdl_flags |= NFSCLDL_RECALL;
			    free(tdp, M_NFSCLDELEG);
			} else {
			    TAILQ_INSERT_HEAD(&extra_deleg, tdp, nfsdl_list);
			}
		    }
		}
		if (error) {
		    if (nop != NULL)
			free(nop, M_NFSCLOPEN);
		    /*
		     * Couldn't reclaim it, so throw the state
		     * away. Ouch!!
		     */
		    nfscl_cleandeleg(dp);
		    nfscl_freedeleg(&clp->nfsc_deleg, dp);
		} else {
		    LIST_INSERT_HEAD(&extra_open, nop, nfso_list);
		}
	    }
	    dp = ndp;
	}

	/*
	 * Now, get rid of extra Opens and Delegations.
	 */
	LIST_FOREACH_SAFE(op, &extra_open, nfso_list, nop) {
		do {
			newnfs_copycred(&op->nfso_cred, tcred);
			error = nfscl_tryclose(op, tcred, nmp, p);
			if (error == NFSERR_GRACE)
				(void) nfs_catnap(PZERO, error, "nfsexcls");
		} while (error == NFSERR_GRACE);
		LIST_REMOVE(op, nfso_list);
		free(op, M_NFSCLOPEN);
	}
	if (nowp != NULL)
		free(nowp, M_NFSCLOWNER);

	TAILQ_FOREACH_SAFE(dp, &extra_deleg, nfsdl_list, ndp) {
		do {
			newnfs_copycred(&dp->nfsdl_cred, tcred);
			error = nfscl_trydelegreturn(dp, tcred, nmp, p);
			if (error == NFSERR_GRACE)
				(void) nfs_catnap(PZERO, error, "nfsexdlg");
		} while (error == NFSERR_GRACE);
		TAILQ_REMOVE(&extra_deleg, dp, nfsdl_list);
		free(dp, M_NFSCLDELEG);
	}

	/* For NFSv4.1 or later, do a RECLAIM_COMPLETE. */
	if (NFSHASNFSV4N(nmp))
		(void)nfsrpc_reclaimcomplete(nmp, cred, p);

	NFSLOCKCLSTATE();
	clp->nfsc_flags &= ~NFSCLFLAGS_RECVRINPROG;
	wakeup(&clp->nfsc_flags);
	nfsv4_unlock(&clp->nfsc_lock, 0);
	NFSUNLOCKCLSTATE();
	NFSFREECRED(tcred);
}

/*
 * This function is called when a server replies with NFSERR_EXPIRED.
 * It deletes all state for the client and does a fresh SetClientId/confirm.
 * XXX Someday it should post a signal to the process(es) that hold the
 * state, so they know that lock state has been lost.
 */
APPLESTATIC int
nfscl_hasexpired(struct nfsclclient *clp, u_int32_t clidrev, NFSPROC_T *p)
{
	struct nfsmount *nmp;
	struct ucred *cred;
	int igotlock = 0, error, trycnt;

	/*
	 * If the clientid has gone away or a new SetClientid has already
	 * been done, just return ok.
	 */
	if (clp == NULL || clidrev != clp->nfsc_clientidrev)
		return (0);

	/*
	 * First, lock the client structure, so everyone else will
	 * block when trying to use state. Also, use NFSCLFLAGS_EXPIREIT so
	 * that only one thread does the work.
	 */
	NFSLOCKCLSTATE();
	clp->nfsc_flags |= NFSCLFLAGS_EXPIREIT;
	do {
		igotlock = nfsv4_lock(&clp->nfsc_lock, 1, NULL,
		    NFSCLSTATEMUTEXPTR, NULL);
	} while (!igotlock && (clp->nfsc_flags & NFSCLFLAGS_EXPIREIT));
	if ((clp->nfsc_flags & NFSCLFLAGS_EXPIREIT) == 0) {
		if (igotlock)
			nfsv4_unlock(&clp->nfsc_lock, 0);
		NFSUNLOCKCLSTATE();
		return (0);
	}
	clp->nfsc_flags |= NFSCLFLAGS_RECVRINPROG;
	NFSUNLOCKCLSTATE();

	nmp = clp->nfsc_nmp;
	if (nmp == NULL)
		panic("nfscl expired");
	cred = newnfs_getcred();
	trycnt = 5;
	do {
		error = nfsrpc_setclient(nmp, clp, 0, cred, p);
	} while ((error == NFSERR_STALECLIENTID ||
	     error == NFSERR_BADSESSION ||
	     error == NFSERR_STALEDONTRECOVER) && --trycnt > 0);
	if (error) {
		NFSLOCKCLSTATE();
		clp->nfsc_flags &= ~NFSCLFLAGS_RECOVER;
	} else {
		/*
		 * Expire the state for the client.
		 */
		nfscl_expireclient(clp, nmp, cred, p);
		NFSLOCKCLSTATE();
		clp->nfsc_flags |= NFSCLFLAGS_HASCLIENTID;
		clp->nfsc_flags &= ~NFSCLFLAGS_RECOVER;
	}
	clp->nfsc_flags &= ~(NFSCLFLAGS_EXPIREIT | NFSCLFLAGS_RECVRINPROG);
	wakeup(&clp->nfsc_flags);
	nfsv4_unlock(&clp->nfsc_lock, 0);
	NFSUNLOCKCLSTATE();
	NFSFREECRED(cred);
	return (error);
}

/*
 * This function inserts a lock in the list after insert_lop.
 */
static void
nfscl_insertlock(struct nfscllockowner *lp, struct nfscllock *new_lop,
    struct nfscllock *insert_lop, int local)
{

	if ((struct nfscllockowner *)insert_lop == lp)
		LIST_INSERT_HEAD(&lp->nfsl_lock, new_lop, nfslo_list);
	else
		LIST_INSERT_AFTER(insert_lop, new_lop, nfslo_list);
	if (local)
		nfsstatsv1.cllocallocks++;
	else
		nfsstatsv1.cllocks++;
}

/*
 * This function updates the locking for a lock owner and given file. It
 * maintains a list of lock ranges ordered on increasing file offset that
 * are NFSCLLOCK_READ or NFSCLLOCK_WRITE and non-overlapping (aka POSIX style).
 * It always adds new_lop to the list and sometimes uses the one pointed
 * at by other_lopp.
 * Returns 1 if the locks were modified, 0 otherwise.
 */
static int
nfscl_updatelock(struct nfscllockowner *lp, struct nfscllock **new_lopp,
    struct nfscllock **other_lopp, int local)
{
	struct nfscllock *new_lop = *new_lopp;
	struct nfscllock *lop, *tlop, *ilop;
	struct nfscllock *other_lop;
	int unlock = 0, modified = 0;
	u_int64_t tmp;

	/*
	 * Work down the list until the lock is merged.
	 */
	if (new_lop->nfslo_type == F_UNLCK)
		unlock = 1;
	ilop = (struct nfscllock *)lp;
	lop = LIST_FIRST(&lp->nfsl_lock);
	while (lop != NULL) {
	    /*
	     * Only check locks for this file that aren't before the start of
	     * new lock's range.
	     */
	    if (lop->nfslo_end >= new_lop->nfslo_first) {
		if (new_lop->nfslo_end < lop->nfslo_first) {
		    /*
		     * If the new lock ends before the start of the
		     * current lock's range, no merge, just insert
		     * the new lock.
		     */
		    break;
		}
		if (new_lop->nfslo_type == lop->nfslo_type ||
		    (new_lop->nfslo_first <= lop->nfslo_first &&
		     new_lop->nfslo_end >= lop->nfslo_end)) {
		    /*
		     * This lock can be absorbed by the new lock/unlock.
		     * This happens when it covers the entire range
		     * of the old lock or is contiguous
		     * with the old lock and is of the same type or an
		     * unlock.
		     */
		    if (new_lop->nfslo_type != lop->nfslo_type ||
			new_lop->nfslo_first != lop->nfslo_first ||
			new_lop->nfslo_end != lop->nfslo_end)
			modified = 1;
		    if (lop->nfslo_first < new_lop->nfslo_first)
			new_lop->nfslo_first = lop->nfslo_first;
		    if (lop->nfslo_end > new_lop->nfslo_end)
			new_lop->nfslo_end = lop->nfslo_end;
		    tlop = lop;
		    lop = LIST_NEXT(lop, nfslo_list);
		    nfscl_freelock(tlop, local);
		    continue;
		}

		/*
		 * All these cases are for contiguous locks that are not the
		 * same type, so they can't be merged.
		 */
		if (new_lop->nfslo_first <= lop->nfslo_first) {
		    /*
		     * This case is where the new lock overlaps with the
		     * first part of the old lock. Move the start of the
		     * old lock to just past the end of the new lock. The
		     * new lock will be inserted in front of the old, since
		     * ilop hasn't been updated. (We are done now.)
		     */
		    if (lop->nfslo_first != new_lop->nfslo_end) {
			lop->nfslo_first = new_lop->nfslo_end;
			modified = 1;
		    }
		    break;
		}
		if (new_lop->nfslo_end >= lop->nfslo_end) {
		    /*
		     * This case is where the new lock overlaps with the
		     * end of the old lock's range. Move the old lock's
		     * end to just before the new lock's first and insert
		     * the new lock after the old lock.
		     * Might not be done yet, since the new lock could
		     * overlap further locks with higher ranges.
		     */
		    if (lop->nfslo_end != new_lop->nfslo_first) {
			lop->nfslo_end = new_lop->nfslo_first;
			modified = 1;
		    }
		    ilop = lop;
		    lop = LIST_NEXT(lop, nfslo_list);
		    continue;
		}
		/*
		 * The final case is where the new lock's range is in the
		 * middle of the current lock's and splits the current lock
		 * up. Use *other_lopp to handle the second part of the
		 * split old lock range. (We are done now.)
		 * For unlock, we use new_lop as other_lop and tmp, since
		 * other_lop and new_lop are the same for this case.
		 * We noted the unlock case above, so we don't need
		 * new_lop->nfslo_type any longer.
		 */
		tmp = new_lop->nfslo_first;
		if (unlock) {
		    other_lop = new_lop;
		    *new_lopp = NULL;
		} else {
		    other_lop = *other_lopp;
		    *other_lopp = NULL;
		}
		other_lop->nfslo_first = new_lop->nfslo_end;
		other_lop->nfslo_end = lop->nfslo_end;
		other_lop->nfslo_type = lop->nfslo_type;
		lop->nfslo_end = tmp;
		nfscl_insertlock(lp, other_lop, lop, local);
		ilop = lop;
		modified = 1;
		break;
	    }
	    ilop = lop;
	    lop = LIST_NEXT(lop, nfslo_list);
	    if (lop == NULL)
		break;
	}

	/*
	 * Insert the new lock in the list at the appropriate place.
	 */
	if (!unlock) {
		nfscl_insertlock(lp, new_lop, ilop, local);
		*new_lopp = NULL;
		modified = 1;
	}
	return (modified);
}

/*
 * This function must be run as a kernel thread.
 * It does Renew Ops and recovery, when required.
 */
APPLESTATIC void
nfscl_renewthread(struct nfsclclient *clp, NFSPROC_T *p)
{
	struct nfsclowner *owp, *nowp;
	struct nfsclopen *op;
	struct nfscllockowner *lp, *nlp;
	struct nfscldeleghead dh;
	struct nfscldeleg *dp, *ndp;
	struct ucred *cred;
	u_int32_t clidrev;
	int error, cbpathdown, islept, igotlock, ret, clearok;
	uint32_t recover_done_time = 0;
	time_t mytime;
	static time_t prevsec = 0;
	struct nfscllockownerfh *lfhp, *nlfhp;
	struct nfscllockownerfhhead lfh;
	struct nfscllayout *lyp, *nlyp;
	struct nfscldevinfo *dip, *ndip;
	struct nfscllayouthead rlh;
	struct nfsclrecalllayout *recallp;
	struct nfsclds *dsp;

	cred = newnfs_getcred();
	NFSLOCKCLSTATE();
	clp->nfsc_flags |= NFSCLFLAGS_HASTHREAD;
	NFSUNLOCKCLSTATE();
	for(;;) {
		newnfs_setroot(cred);
		cbpathdown = 0;
		if (clp->nfsc_flags & NFSCLFLAGS_RECOVER) {
			/*
			 * Only allow one recover within 1/2 of the lease
			 * duration (nfsc_renew).
			 */
			if (recover_done_time < NFSD_MONOSEC) {
				recover_done_time = NFSD_MONOSEC +
				    clp->nfsc_renew;
				NFSCL_DEBUG(1, "Doing recovery..\n");
				nfscl_recover(clp, cred, p);
			} else {
				NFSCL_DEBUG(1, "Clear Recovery dt=%u ms=%jd\n",
				    recover_done_time, (intmax_t)NFSD_MONOSEC);
				NFSLOCKCLSTATE();
				clp->nfsc_flags &= ~NFSCLFLAGS_RECOVER;
				NFSUNLOCKCLSTATE();
			}
		}
		if (clp->nfsc_expire <= NFSD_MONOSEC &&
		    (clp->nfsc_flags & NFSCLFLAGS_HASCLIENTID)) {
			clp->nfsc_expire = NFSD_MONOSEC + clp->nfsc_renew;
			clidrev = clp->nfsc_clientidrev;
			error = nfsrpc_renew(clp, NULL, cred, p);
			if (error == NFSERR_CBPATHDOWN)
			    cbpathdown = 1;
			else if (error == NFSERR_STALECLIENTID ||
			    error == NFSERR_BADSESSION) {
			    NFSLOCKCLSTATE();
			    clp->nfsc_flags |= NFSCLFLAGS_RECOVER;
			    NFSUNLOCKCLSTATE();
			} else if (error == NFSERR_EXPIRED)
			    (void) nfscl_hasexpired(clp, clidrev, p);
		}

checkdsrenew:
		if (NFSHASNFSV4N(clp->nfsc_nmp)) {
			/* Do renews for any DS sessions. */
			NFSLOCKMNT(clp->nfsc_nmp);
			/* Skip first entry, since the MDS is handled above. */
			dsp = TAILQ_FIRST(&clp->nfsc_nmp->nm_sess);
			if (dsp != NULL)
				dsp = TAILQ_NEXT(dsp, nfsclds_list);
			while (dsp != NULL) {
				if (dsp->nfsclds_expire <= NFSD_MONOSEC &&
				    dsp->nfsclds_sess.nfsess_defunct == 0) {
					dsp->nfsclds_expire = NFSD_MONOSEC +
					    clp->nfsc_renew;
					NFSUNLOCKMNT(clp->nfsc_nmp);
					(void)nfsrpc_renew(clp, dsp, cred, p);
					goto checkdsrenew;
				}
				dsp = TAILQ_NEXT(dsp, nfsclds_list);
			}
			NFSUNLOCKMNT(clp->nfsc_nmp);
		}

		TAILQ_INIT(&dh);
		NFSLOCKCLSTATE();
		if (cbpathdown)
			/* It's a Total Recall! */
			nfscl_totalrecall(clp);

		/*
		 * Now, handle defunct owners.
		 */
		LIST_FOREACH_SAFE(owp, &clp->nfsc_owner, nfsow_list, nowp) {
			if (LIST_EMPTY(&owp->nfsow_open)) {
				if (owp->nfsow_defunct != 0)
					nfscl_freeopenowner(owp, 0);
			}
		}

		/*
		 * Do the recall on any delegations. To avoid trouble, always
		 * come back up here after having slept.
		 */
		igotlock = 0;
tryagain:
		dp = TAILQ_FIRST(&clp->nfsc_deleg);
		while (dp != NULL) {
			ndp = TAILQ_NEXT(dp, nfsdl_list);
			if ((dp->nfsdl_flags & NFSCLDL_RECALL)) {
				/*
				 * Wait for outstanding I/O ops to be done.
				 */
				if (dp->nfsdl_rwlock.nfslock_usecnt > 0) {
				    if (igotlock) {
					nfsv4_unlock(&clp->nfsc_lock, 0);
					igotlock = 0;
				    }
				    dp->nfsdl_rwlock.nfslock_lock |=
					NFSV4LOCK_WANTED;
				    (void) nfsmsleep(&dp->nfsdl_rwlock,
					NFSCLSTATEMUTEXPTR, PZERO, "nfscld",
					NULL);
				    goto tryagain;
				}
				while (!igotlock) {
				    igotlock = nfsv4_lock(&clp->nfsc_lock, 1,
					&islept, NFSCLSTATEMUTEXPTR, NULL);
				    if (islept)
					goto tryagain;
				}
				NFSUNLOCKCLSTATE();
				newnfs_copycred(&dp->nfsdl_cred, cred);
				ret = nfscl_recalldeleg(clp, clp->nfsc_nmp, dp,
				    NULL, cred, p, 1);
				if (!ret) {
				    nfscl_cleandeleg(dp);
				    TAILQ_REMOVE(&clp->nfsc_deleg, dp,
					nfsdl_list);
				    LIST_REMOVE(dp, nfsdl_hash);
				    TAILQ_INSERT_HEAD(&dh, dp, nfsdl_list);
				    nfscl_delegcnt--;
				    nfsstatsv1.cldelegates--;
				}
				NFSLOCKCLSTATE();
			}
			dp = ndp;
		}

		/*
		 * Clear out old delegations, if we are above the high water
		 * mark. Only clear out ones with no state related to them.
		 * The tailq list is in LRU order.
		 */
		dp = TAILQ_LAST(&clp->nfsc_deleg, nfscldeleghead);
		while (nfscl_delegcnt > nfscl_deleghighwater && dp != NULL) {
		    ndp = TAILQ_PREV(dp, nfscldeleghead, nfsdl_list);
		    if (dp->nfsdl_rwlock.nfslock_usecnt == 0 &&
			dp->nfsdl_rwlock.nfslock_lock == 0 &&
			dp->nfsdl_timestamp < NFSD_MONOSEC &&
			(dp->nfsdl_flags & (NFSCLDL_RECALL | NFSCLDL_ZAPPED |
			  NFSCLDL_NEEDRECLAIM | NFSCLDL_DELEGRET)) == 0) {
			clearok = 1;
			LIST_FOREACH(owp, &dp->nfsdl_owner, nfsow_list) {
			    op = LIST_FIRST(&owp->nfsow_open);
			    if (op != NULL) {
				clearok = 0;
				break;
			    }
			}
			if (clearok) {
			    LIST_FOREACH(lp, &dp->nfsdl_lock, nfsl_list) {
				if (!LIST_EMPTY(&lp->nfsl_lock)) {
				    clearok = 0;
				    break;
				}
			    }
			}
			if (clearok) {
			    TAILQ_REMOVE(&clp->nfsc_deleg, dp, nfsdl_list);
			    LIST_REMOVE(dp, nfsdl_hash);
			    TAILQ_INSERT_HEAD(&dh, dp, nfsdl_list);
			    nfscl_delegcnt--;
			    nfsstatsv1.cldelegates--;
			}
		    }
		    dp = ndp;
		}
		if (igotlock)
			nfsv4_unlock(&clp->nfsc_lock, 0);

		/*
		 * Do the recall on any layouts. To avoid trouble, always
		 * come back up here after having slept.
		 */
		TAILQ_INIT(&rlh);
tryagain2:
		TAILQ_FOREACH_SAFE(lyp, &clp->nfsc_layout, nfsly_list, nlyp) {
			if ((lyp->nfsly_flags & NFSLY_RECALL) != 0) {
				/*
				 * Wait for outstanding I/O ops to be done.
				 */
				if (lyp->nfsly_lock.nfslock_usecnt > 0 ||
				    (lyp->nfsly_lock.nfslock_lock &
				     NFSV4LOCK_LOCK) != 0) {
					lyp->nfsly_lock.nfslock_lock |=
					    NFSV4LOCK_WANTED;
					nfsmsleep(&lyp->nfsly_lock.nfslock_lock,
					    NFSCLSTATEMUTEXPTR, PZERO, "nfslyp",
					    NULL);
					goto tryagain2;
				}
				/* Move the layout to the recall list. */
				TAILQ_REMOVE(&clp->nfsc_layout, lyp,
				    nfsly_list);
				LIST_REMOVE(lyp, nfsly_hash);
				TAILQ_INSERT_HEAD(&rlh, lyp, nfsly_list);

				/* Handle any layout commits. */
				if (!NFSHASNOLAYOUTCOMMIT(clp->nfsc_nmp) &&
				    (lyp->nfsly_flags & NFSLY_WRITTEN) != 0) {
					lyp->nfsly_flags &= ~NFSLY_WRITTEN;
					NFSUNLOCKCLSTATE();
					NFSCL_DEBUG(3, "do layoutcommit\n");
					nfscl_dolayoutcommit(clp->nfsc_nmp, lyp,
					    cred, p);
					NFSLOCKCLSTATE();
					goto tryagain2;
				}
			}
		}

		/* Now, look for stale layouts. */
		lyp = TAILQ_LAST(&clp->nfsc_layout, nfscllayouthead);
		while (lyp != NULL) {
			nlyp = TAILQ_PREV(lyp, nfscllayouthead, nfsly_list);
			if (lyp->nfsly_timestamp < NFSD_MONOSEC &&
			    (lyp->nfsly_flags & NFSLY_RECALL) == 0 &&
			    lyp->nfsly_lock.nfslock_usecnt == 0 &&
			    lyp->nfsly_lock.nfslock_lock == 0) {
				NFSCL_DEBUG(4, "ret stale lay=%d\n",
				    nfscl_layoutcnt);
				recallp = malloc(sizeof(*recallp),
				    M_NFSLAYRECALL, M_NOWAIT);
				if (recallp == NULL)
					break;
				(void)nfscl_layoutrecall(NFSLAYOUTRETURN_FILE,
				    lyp, NFSLAYOUTIOMODE_ANY, 0, UINT64_MAX,
				    lyp->nfsly_stateid.seqid, 0, 0, NULL,
				    recallp);
			}
			lyp = nlyp;
		}

		/*
		 * Free up any unreferenced device info structures.
		 */
		LIST_FOREACH_SAFE(dip, &clp->nfsc_devinfo, nfsdi_list, ndip) {
			if (dip->nfsdi_layoutrefs == 0 &&
			    dip->nfsdi_refcnt == 0) {
				NFSCL_DEBUG(4, "freeing devinfo\n");
				LIST_REMOVE(dip, nfsdi_list);
				nfscl_freedevinfo(dip);
			}
		}
		NFSUNLOCKCLSTATE();

		/* Do layout return(s), as required. */
		TAILQ_FOREACH_SAFE(lyp, &rlh, nfsly_list, nlyp) {
			TAILQ_REMOVE(&rlh, lyp, nfsly_list);
			NFSCL_DEBUG(4, "ret layout\n");
			nfscl_layoutreturn(clp->nfsc_nmp, lyp, cred, p);
			nfscl_freelayout(lyp);
		}

		/*
		 * Delegreturn any delegations cleaned out or recalled.
		 */
		TAILQ_FOREACH_SAFE(dp, &dh, nfsdl_list, ndp) {
			newnfs_copycred(&dp->nfsdl_cred, cred);
			(void) nfscl_trydelegreturn(dp, cred, clp->nfsc_nmp, p);
			TAILQ_REMOVE(&dh, dp, nfsdl_list);
			free(dp, M_NFSCLDELEG);
		}

		SLIST_INIT(&lfh);
		/*
		 * Call nfscl_cleanupkext() once per second to check for
		 * open/lock owners where the process has exited.
		 */
		mytime = NFSD_MONOSEC;
		if (prevsec != mytime) {
			prevsec = mytime;
			nfscl_cleanupkext(clp, &lfh);
		}

		/*
		 * Do a ReleaseLockOwner for all lock owners where the
		 * associated process no longer exists, as found by
		 * nfscl_cleanupkext().
		 */
		newnfs_setroot(cred);
		SLIST_FOREACH_SAFE(lfhp, &lfh, nfslfh_list, nlfhp) {
			LIST_FOREACH_SAFE(lp, &lfhp->nfslfh_lock, nfsl_list,
			    nlp) {
				(void)nfsrpc_rellockown(clp->nfsc_nmp, lp,
				    lfhp->nfslfh_fh, lfhp->nfslfh_len, cred,
				    p);
				nfscl_freelockowner(lp, 0);
			}
			free(lfhp, M_TEMP);
		}
		SLIST_INIT(&lfh);

		NFSLOCKCLSTATE();
		if ((clp->nfsc_flags & NFSCLFLAGS_RECOVER) == 0)
			(void)mtx_sleep(clp, NFSCLSTATEMUTEXPTR, PWAIT, "nfscl",
			    hz);
		if (clp->nfsc_flags & NFSCLFLAGS_UMOUNT) {
			clp->nfsc_flags &= ~NFSCLFLAGS_HASTHREAD;
			NFSUNLOCKCLSTATE();
			NFSFREECRED(cred);
			wakeup((caddr_t)clp);
			return;
		}
		NFSUNLOCKCLSTATE();
	}
}

/*
 * Initiate state recovery. Called when NFSERR_STALECLIENTID,
 * NFSERR_STALESTATEID or NFSERR_BADSESSION is received.
 */
APPLESTATIC void
nfscl_initiate_recovery(struct nfsclclient *clp)
{

	if (clp == NULL)
		return;
	NFSLOCKCLSTATE();
	clp->nfsc_flags |= NFSCLFLAGS_RECOVER;
	NFSUNLOCKCLSTATE();
	wakeup((caddr_t)clp);
}

/*
 * Dump out the state stuff for debugging.
 */
APPLESTATIC void
nfscl_dumpstate(struct nfsmount *nmp, int openowner, int opens,
    int lockowner, int locks)
{
	struct nfsclclient *clp;
	struct nfsclowner *owp;
	struct nfsclopen *op;
	struct nfscllockowner *lp;
	struct nfscllock *lop;
	struct nfscldeleg *dp;

	clp = nmp->nm_clp;
	if (clp == NULL) {
		printf("nfscl dumpstate NULL clp\n");
		return;
	}
	NFSLOCKCLSTATE();
	TAILQ_FOREACH(dp, &clp->nfsc_deleg, nfsdl_list) {
	  LIST_FOREACH(owp, &dp->nfsdl_owner, nfsow_list) {
	    if (openowner && !LIST_EMPTY(&owp->nfsow_open))
		printf("owner=0x%x 0x%x 0x%x 0x%x seqid=%d\n",
		    owp->nfsow_owner[0], owp->nfsow_owner[1],
		    owp->nfsow_owner[2], owp->nfsow_owner[3],
		    owp->nfsow_seqid);
	    LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
		if (opens)
		    printf("open st=0x%x 0x%x 0x%x cnt=%d fh12=0x%x\n",
			op->nfso_stateid.other[0], op->nfso_stateid.other[1],
			op->nfso_stateid.other[2], op->nfso_opencnt,
			op->nfso_fh[12]);
		LIST_FOREACH(lp, &op->nfso_lock, nfsl_list) {
		    if (lockowner)
			printf("lckown=0x%x 0x%x 0x%x 0x%x seqid=%d st=0x%x 0x%x 0x%x\n",
			    lp->nfsl_owner[0], lp->nfsl_owner[1],
			    lp->nfsl_owner[2], lp->nfsl_owner[3],
			    lp->nfsl_seqid,
			    lp->nfsl_stateid.other[0], lp->nfsl_stateid.other[1],
			    lp->nfsl_stateid.other[2]);
		    LIST_FOREACH(lop, &lp->nfsl_lock, nfslo_list) {
			if (locks)
#ifdef __FreeBSD__
			    printf("lck typ=%d fst=%ju end=%ju\n",
				lop->nfslo_type, (intmax_t)lop->nfslo_first,
				(intmax_t)lop->nfslo_end);
#else
			    printf("lck typ=%d fst=%qd end=%qd\n",
				lop->nfslo_type, lop->nfslo_first,
				lop->nfslo_end);
#endif
		    }
		}
	    }
	  }
	}
	LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
	    if (openowner && !LIST_EMPTY(&owp->nfsow_open))
		printf("owner=0x%x 0x%x 0x%x 0x%x seqid=%d\n",
		    owp->nfsow_owner[0], owp->nfsow_owner[1],
		    owp->nfsow_owner[2], owp->nfsow_owner[3],
		    owp->nfsow_seqid);
	    LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
		if (opens)
		    printf("open st=0x%x 0x%x 0x%x cnt=%d fh12=0x%x\n",
			op->nfso_stateid.other[0], op->nfso_stateid.other[1],
			op->nfso_stateid.other[2], op->nfso_opencnt,
			op->nfso_fh[12]);
		LIST_FOREACH(lp, &op->nfso_lock, nfsl_list) {
		    if (lockowner)
			printf("lckown=0x%x 0x%x 0x%x 0x%x seqid=%d st=0x%x 0x%x 0x%x\n",
			    lp->nfsl_owner[0], lp->nfsl_owner[1],
			    lp->nfsl_owner[2], lp->nfsl_owner[3],
			    lp->nfsl_seqid,
			    lp->nfsl_stateid.other[0], lp->nfsl_stateid.other[1],
			    lp->nfsl_stateid.other[2]);
		    LIST_FOREACH(lop, &lp->nfsl_lock, nfslo_list) {
			if (locks)
#ifdef __FreeBSD__
			    printf("lck typ=%d fst=%ju end=%ju\n",
				lop->nfslo_type, (intmax_t)lop->nfslo_first,
				(intmax_t)lop->nfslo_end);
#else
			    printf("lck typ=%d fst=%qd end=%qd\n",
				lop->nfslo_type, lop->nfslo_first,
				lop->nfslo_end);
#endif
		    }
		}
	    }
	}
	NFSUNLOCKCLSTATE();
}

/*
 * Check for duplicate open owners and opens.
 * (Only used as a diagnostic aid.)
 */
APPLESTATIC void
nfscl_dupopen(vnode_t vp, int dupopens)
{
	struct nfsclclient *clp;
	struct nfsclowner *owp, *owp2;
	struct nfsclopen *op, *op2;
	struct nfsfh *nfhp;

	clp = VFSTONFS(vnode_mount(vp))->nm_clp;
	if (clp == NULL) {
		printf("nfscl dupopen NULL clp\n");
		return;
	}
	nfhp = VTONFS(vp)->n_fhp;
	NFSLOCKCLSTATE();

	/*
	 * First, search for duplicate owners.
	 * These should never happen!
	 */
	LIST_FOREACH(owp2, &clp->nfsc_owner, nfsow_list) {
	    LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
		if (owp != owp2 &&
		    !NFSBCMP(owp->nfsow_owner, owp2->nfsow_owner,
		    NFSV4CL_LOCKNAMELEN)) {
			NFSUNLOCKCLSTATE();
			printf("DUP OWNER\n");
			nfscl_dumpstate(VFSTONFS(vnode_mount(vp)), 1, 1, 0, 0);
			return;
		}
	    }
	}

	/*
	 * Now, search for duplicate stateids.
	 * These shouldn't happen, either.
	 */
	LIST_FOREACH(owp2, &clp->nfsc_owner, nfsow_list) {
	    LIST_FOREACH(op2, &owp2->nfsow_open, nfso_list) {
		LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
		    LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
			if (op != op2 &&
			    (op->nfso_stateid.other[0] != 0 ||
			     op->nfso_stateid.other[1] != 0 ||
			     op->nfso_stateid.other[2] != 0) &&
			    op->nfso_stateid.other[0] == op2->nfso_stateid.other[0] &&
			    op->nfso_stateid.other[1] == op2->nfso_stateid.other[1] &&
			    op->nfso_stateid.other[2] == op2->nfso_stateid.other[2]) {
			    NFSUNLOCKCLSTATE();
			    printf("DUP STATEID\n");
			    nfscl_dumpstate(VFSTONFS(vnode_mount(vp)), 1, 1, 0,
				0);
			    return;
			}
		    }
		}
	    }
	}

	/*
	 * Now search for duplicate opens.
	 * Duplicate opens for the same owner
	 * should never occur. Other duplicates are
	 * possible and are checked for if "dupopens"
	 * is true.
	 */
	LIST_FOREACH(owp2, &clp->nfsc_owner, nfsow_list) {
	    LIST_FOREACH(op2, &owp2->nfsow_open, nfso_list) {
		if (nfhp->nfh_len == op2->nfso_fhlen &&
		    !NFSBCMP(nfhp->nfh_fh, op2->nfso_fh, nfhp->nfh_len)) {
		    LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
			LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
			    if (op != op2 && nfhp->nfh_len == op->nfso_fhlen &&
				!NFSBCMP(nfhp->nfh_fh, op->nfso_fh, nfhp->nfh_len) &&
				(!NFSBCMP(op->nfso_own->nfsow_owner,
				 op2->nfso_own->nfsow_owner, NFSV4CL_LOCKNAMELEN) ||
				 dupopens)) {
				if (!NFSBCMP(op->nfso_own->nfsow_owner,
				    op2->nfso_own->nfsow_owner, NFSV4CL_LOCKNAMELEN)) {
				    NFSUNLOCKCLSTATE();
				    printf("BADDUP OPEN\n");
				} else {
				    NFSUNLOCKCLSTATE();
				    printf("DUP OPEN\n");
				}
				nfscl_dumpstate(VFSTONFS(vnode_mount(vp)), 1, 1,
				    0, 0);
				return;
			    }
			}
		    }
		}
	    }
	}
	NFSUNLOCKCLSTATE();
}

/*
 * During close, find an open that needs to be dereferenced and
 * dereference it. If there are no more opens for this file,
 * log a message to that effect.
 * Opens aren't actually Close'd until VOP_INACTIVE() is performed
 * on the file's vnode.
 * This is the safe way, since it is difficult to identify
 * which open the close is for and I/O can be performed after the
 * close(2) system call when a file is mmap'd.
 * If it returns 0 for success, there will be a referenced
 * clp returned via clpp.
 */
APPLESTATIC int
nfscl_getclose(vnode_t vp, struct nfsclclient **clpp)
{
	struct nfsclclient *clp;
	struct nfsclowner *owp;
	struct nfsclopen *op;
	struct nfscldeleg *dp;
	struct nfsfh *nfhp;
	int error, notdecr;

	error = nfscl_getcl(vnode_mount(vp), NULL, NULL, 1, &clp);
	if (error)
		return (error);
	*clpp = clp;

	nfhp = VTONFS(vp)->n_fhp;
	notdecr = 1;
	NFSLOCKCLSTATE();
	/*
	 * First, look for one under a delegation that was locally issued
	 * and just decrement the opencnt for it. Since all my Opens against
	 * the server are DENY_NONE, I don't see a problem with hanging
	 * onto them. (It is much easier to use one of the extant Opens
	 * that I already have on the server when a Delegation is recalled
	 * than to do fresh Opens.) Someday, I might need to rethink this, but.
	 */
	dp = nfscl_finddeleg(clp, nfhp->nfh_fh, nfhp->nfh_len);
	if (dp != NULL) {
		LIST_FOREACH(owp, &dp->nfsdl_owner, nfsow_list) {
			op = LIST_FIRST(&owp->nfsow_open);
			if (op != NULL) {
				/*
				 * Since a delegation is for a file, there
				 * should never be more than one open for
				 * each openowner.
				 */
				if (LIST_NEXT(op, nfso_list) != NULL)
					panic("nfscdeleg opens");
				if (notdecr && op->nfso_opencnt > 0) {
					notdecr = 0;
					op->nfso_opencnt--;
					break;
				}
			}
		}
	}

	/* Now process the opens against the server. */
	LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
		LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
			if (op->nfso_fhlen == nfhp->nfh_len &&
			    !NFSBCMP(op->nfso_fh, nfhp->nfh_fh,
			    nfhp->nfh_len)) {
				/* Found an open, decrement cnt if possible */
				if (notdecr && op->nfso_opencnt > 0) {
					notdecr = 0;
					op->nfso_opencnt--;
				}
				/*
				 * There are more opens, so just return.
				 */
				if (op->nfso_opencnt > 0) {
					NFSUNLOCKCLSTATE();
					return (0);
				}
			}
		}
	}
	NFSUNLOCKCLSTATE();
	if (notdecr)
		printf("nfscl: never fnd open\n");
	return (0);
}

APPLESTATIC int
nfscl_doclose(vnode_t vp, struct nfsclclient **clpp, NFSPROC_T *p)
{
	struct nfsclclient *clp;
	struct nfsclowner *owp, *nowp;
	struct nfsclopen *op;
	struct nfscldeleg *dp;
	struct nfsfh *nfhp;
	struct nfsclrecalllayout *recallp;
	int error;

	error = nfscl_getcl(vnode_mount(vp), NULL, NULL, 1, &clp);
	if (error)
		return (error);
	*clpp = clp;

	nfhp = VTONFS(vp)->n_fhp;
	recallp = malloc(sizeof(*recallp), M_NFSLAYRECALL, M_WAITOK);
	NFSLOCKCLSTATE();
	/*
	 * First get rid of the local Open structures, which should be no
	 * longer in use.
	 */
	dp = nfscl_finddeleg(clp, nfhp->nfh_fh, nfhp->nfh_len);
	if (dp != NULL) {
		LIST_FOREACH_SAFE(owp, &dp->nfsdl_owner, nfsow_list, nowp) {
			op = LIST_FIRST(&owp->nfsow_open);
			if (op != NULL) {
				KASSERT((op->nfso_opencnt == 0),
				    ("nfscl: bad open cnt on deleg"));
				nfscl_freeopen(op, 1);
			}
			nfscl_freeopenowner(owp, 1);
		}
	}

	/* Return any layouts marked return on close. */
	nfscl_retoncloselayout(vp, clp, nfhp->nfh_fh, nfhp->nfh_len, &recallp);

	/* Now process the opens against the server. */
lookformore:
	LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
		op = LIST_FIRST(&owp->nfsow_open);
		while (op != NULL) {
			if (op->nfso_fhlen == nfhp->nfh_len &&
			    !NFSBCMP(op->nfso_fh, nfhp->nfh_fh,
			    nfhp->nfh_len)) {
				/* Found an open, close it. */
#ifdef DIAGNOSTIC
				KASSERT((op->nfso_opencnt == 0),
				    ("nfscl: bad open cnt on server (%d)",
				     op->nfso_opencnt));
#endif
				NFSUNLOCKCLSTATE();
				nfsrpc_doclose(VFSTONFS(vnode_mount(vp)), op,
				    p);
				NFSLOCKCLSTATE();
				goto lookformore;
			}
			op = LIST_NEXT(op, nfso_list);
		}
	}
	NFSUNLOCKCLSTATE();
	/*
	 * recallp has been set NULL by nfscl_retoncloselayout() if it was
	 * used by the function, but calling free() with a NULL pointer is ok.
	 */
	free(recallp, M_NFSLAYRECALL);
	return (0);
}

/*
 * Return all delegations on this client.
 * (Must be called with client sleep lock.)
 */
static void
nfscl_delegreturnall(struct nfsclclient *clp, NFSPROC_T *p)
{
	struct nfscldeleg *dp, *ndp;
	struct ucred *cred;

	cred = newnfs_getcred();
	TAILQ_FOREACH_SAFE(dp, &clp->nfsc_deleg, nfsdl_list, ndp) {
		nfscl_cleandeleg(dp);
		(void) nfscl_trydelegreturn(dp, cred, clp->nfsc_nmp, p);
		nfscl_freedeleg(&clp->nfsc_deleg, dp);
	}
	NFSFREECRED(cred);
}

/*
 * Do a callback RPC.
 */
APPLESTATIC void
nfscl_docb(struct nfsrv_descript *nd, NFSPROC_T *p)
{
	int clist, gotseq_ok, i, j, k, op, rcalls;
	u_int32_t *tl;
	struct nfsclclient *clp;
	struct nfscldeleg *dp = NULL;
	int numops, taglen = -1, error = 0, trunc __unused;
	u_int32_t minorvers = 0, retops = 0, *retopsp = NULL, *repp, cbident;
	u_char tag[NFSV4_SMALLSTR + 1], *tagstr;
	vnode_t vp = NULL;
	struct nfsnode *np;
	struct vattr va;
	struct nfsfh *nfhp;
	mount_t mp;
	nfsattrbit_t attrbits, rattrbits;
	nfsv4stateid_t stateid;
	uint32_t seqid, slotid = 0, highslot, cachethis __unused;
	uint8_t sessionid[NFSX_V4SESSIONID];
	struct mbuf *rep;
	struct nfscllayout *lyp;
	uint64_t filesid[2], len, off;
	int changed, gotone, laytype, recalltype;
	uint32_t iomode;
	struct nfsclrecalllayout *recallp = NULL;
	struct nfsclsession *tsep;

	gotseq_ok = 0;
	nfsrvd_rephead(nd);
	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	taglen = fxdr_unsigned(int, *tl);
	if (taglen < 0) {
		error = EBADRPC;
		goto nfsmout;
	}
	if (taglen <= NFSV4_SMALLSTR)
		tagstr = tag;
	else
		tagstr = malloc(taglen + 1, M_TEMP, M_WAITOK);
	error = nfsrv_mtostr(nd, tagstr, taglen);
	if (error) {
		if (taglen > NFSV4_SMALLSTR)
			free(tagstr, M_TEMP);
		taglen = -1;
		goto nfsmout;
	}
	(void) nfsm_strtom(nd, tag, taglen);
	if (taglen > NFSV4_SMALLSTR) {
		free(tagstr, M_TEMP);
	}
	NFSM_BUILD(retopsp, u_int32_t *, NFSX_UNSIGNED);
	NFSM_DISSECT(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
	minorvers = fxdr_unsigned(u_int32_t, *tl++);
	if (minorvers != NFSV4_MINORVERSION && minorvers != NFSV41_MINORVERSION)
		nd->nd_repstat = NFSERR_MINORVERMISMATCH;
	cbident = fxdr_unsigned(u_int32_t, *tl++);
	if (nd->nd_repstat)
		numops = 0;
	else
		numops = fxdr_unsigned(int, *tl);
	/*
	 * Loop around doing the sub ops.
	 */
	for (i = 0; i < numops; i++) {
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		NFSM_BUILD(repp, u_int32_t *, 2 * NFSX_UNSIGNED);
		*repp++ = *tl;
		op = fxdr_unsigned(int, *tl);
		if (op < NFSV4OP_CBGETATTR ||
		   (op > NFSV4OP_CBRECALL && minorvers == NFSV4_MINORVERSION) ||
		   (op > NFSV4OP_CBNOTIFYDEVID &&
		    minorvers == NFSV41_MINORVERSION)) {
		    nd->nd_repstat = NFSERR_OPILLEGAL;
		    *repp = nfscl_errmap(nd, minorvers);
		    retops++;
		    break;
		}
		nd->nd_procnum = op;
		if (op < NFSV41_CBNOPS)
			nfsstatsv1.cbrpccnt[nd->nd_procnum]++;
		switch (op) {
		case NFSV4OP_CBGETATTR:
			NFSCL_DEBUG(4, "cbgetattr\n");
			mp = NULL;
			vp = NULL;
			error = nfsm_getfh(nd, &nfhp);
			if (!error)
				error = nfsrv_getattrbits(nd, &attrbits,
				    NULL, NULL);
			if (error == 0 && i == 0 &&
			    minorvers != NFSV4_MINORVERSION)
				error = NFSERR_OPNOTINSESS;
			if (!error) {
				mp = nfscl_getmnt(minorvers, sessionid, cbident,
				    &clp);
				if (mp == NULL)
					error = NFSERR_SERVERFAULT;
			}
			if (!error) {
				error = nfscl_ngetreopen(mp, nfhp->nfh_fh,
				    nfhp->nfh_len, p, &np);
				if (!error)
					vp = NFSTOV(np);
			}
			if (!error) {
				NFSZERO_ATTRBIT(&rattrbits);
				NFSLOCKCLSTATE();
				dp = nfscl_finddeleg(clp, nfhp->nfh_fh,
				    nfhp->nfh_len);
				if (dp != NULL) {
					if (NFSISSET_ATTRBIT(&attrbits,
					    NFSATTRBIT_SIZE)) {
						if (vp != NULL)
							va.va_size = np->n_size;
						else
							va.va_size =
							    dp->nfsdl_size;
						NFSSETBIT_ATTRBIT(&rattrbits,
						    NFSATTRBIT_SIZE);
					}
					if (NFSISSET_ATTRBIT(&attrbits,
					    NFSATTRBIT_CHANGE)) {
						va.va_filerev =
						    dp->nfsdl_change;
						if (vp == NULL ||
						    (np->n_flag & NDELEGMOD))
							va.va_filerev++;
						NFSSETBIT_ATTRBIT(&rattrbits,
						    NFSATTRBIT_CHANGE);
					}
				} else
					error = NFSERR_SERVERFAULT;
				NFSUNLOCKCLSTATE();
			}
			if (vp != NULL)
				vrele(vp);
			if (mp != NULL)
				vfs_unbusy(mp);
			if (nfhp != NULL)
				free(nfhp, M_NFSFH);
			if (!error)
				(void) nfsv4_fillattr(nd, NULL, NULL, NULL, &va,
				    NULL, 0, &rattrbits, NULL, p, 0, 0, 0, 0,
				    (uint64_t)0, NULL);
			break;
		case NFSV4OP_CBRECALL:
			NFSCL_DEBUG(4, "cbrecall\n");
			NFSM_DISSECT(tl, u_int32_t *, NFSX_STATEID +
			    NFSX_UNSIGNED);
			stateid.seqid = *tl++;
			NFSBCOPY((caddr_t)tl, (caddr_t)stateid.other,
			    NFSX_STATEIDOTHER);
			tl += (NFSX_STATEIDOTHER / NFSX_UNSIGNED);
			trunc = fxdr_unsigned(int, *tl);
			error = nfsm_getfh(nd, &nfhp);
			if (error == 0 && i == 0 &&
			    minorvers != NFSV4_MINORVERSION)
				error = NFSERR_OPNOTINSESS;
			if (!error) {
				NFSLOCKCLSTATE();
				if (minorvers == NFSV4_MINORVERSION)
					clp = nfscl_getclnt(cbident);
				else
					clp = nfscl_getclntsess(sessionid);
				if (clp != NULL) {
					dp = nfscl_finddeleg(clp, nfhp->nfh_fh,
					    nfhp->nfh_len);
					if (dp != NULL && (dp->nfsdl_flags &
					    NFSCLDL_DELEGRET) == 0) {
						dp->nfsdl_flags |=
						    NFSCLDL_RECALL;
						wakeup((caddr_t)clp);
					}
				} else {
					error = NFSERR_SERVERFAULT;
				}
				NFSUNLOCKCLSTATE();
			}
			if (nfhp != NULL)
				free(nfhp, M_NFSFH);
			break;
		case NFSV4OP_CBLAYOUTRECALL:
			NFSCL_DEBUG(4, "cblayrec\n");
			nfhp = NULL;
			NFSM_DISSECT(tl, uint32_t *, 4 * NFSX_UNSIGNED);
			laytype = fxdr_unsigned(int, *tl++);
			iomode = fxdr_unsigned(uint32_t, *tl++);
			if (newnfs_true == *tl++)
				changed = 1;
			else
				changed = 0;
			recalltype = fxdr_unsigned(int, *tl);
			NFSCL_DEBUG(4, "layt=%d iom=%d ch=%d rectyp=%d\n",
			    laytype, iomode, changed, recalltype);
			recallp = malloc(sizeof(*recallp), M_NFSLAYRECALL,
			    M_WAITOK);
			if (laytype != NFSLAYOUT_NFSV4_1_FILES &&
			    laytype != NFSLAYOUT_FLEXFILE)
				error = NFSERR_NOMATCHLAYOUT;
			else if (recalltype == NFSLAYOUTRETURN_FILE) {
				error = nfsm_getfh(nd, &nfhp);
				NFSCL_DEBUG(4, "retfile getfh=%d\n", error);
				if (error != 0)
					goto nfsmout;
				NFSM_DISSECT(tl, u_int32_t *, 2 * NFSX_HYPER +
				    NFSX_STATEID);
				off = fxdr_hyper(tl); tl += 2;
				len = fxdr_hyper(tl); tl += 2;
				stateid.seqid = fxdr_unsigned(uint32_t, *tl++);
				NFSBCOPY(tl, stateid.other, NFSX_STATEIDOTHER);
				if (minorvers == NFSV4_MINORVERSION)
					error = NFSERR_NOTSUPP;
				else if (i == 0)
					error = NFSERR_OPNOTINSESS;
				NFSCL_DEBUG(4, "off=%ju len=%ju sq=%u err=%d\n",
				    (uintmax_t)off, (uintmax_t)len,
				    stateid.seqid, error);
				if (error == 0) {
					NFSLOCKCLSTATE();
					clp = nfscl_getclntsess(sessionid);
					NFSCL_DEBUG(4, "cbly clp=%p\n", clp);
					if (clp != NULL) {
						lyp = nfscl_findlayout(clp,
						    nfhp->nfh_fh,
						    nfhp->nfh_len);
						NFSCL_DEBUG(4, "cblyp=%p\n",
						    lyp);
						if (lyp != NULL &&
						    (lyp->nfsly_flags &
						     (NFSLY_FILES |
						      NFSLY_FLEXFILE)) != 0 &&
						    !NFSBCMP(stateid.other,
						    lyp->nfsly_stateid.other,
						    NFSX_STATEIDOTHER)) {
							error =
							    nfscl_layoutrecall(
							    recalltype,
							    lyp, iomode, off,
							    len, stateid.seqid,
							    0, 0, NULL,
							    recallp);
							recallp = NULL;
							wakeup(clp);
							NFSCL_DEBUG(4,
							    "aft layrcal=%d\n",
							    error);
						} else
							error =
							  NFSERR_NOMATCHLAYOUT;
					} else
						error = NFSERR_NOMATCHLAYOUT;
					NFSUNLOCKCLSTATE();
				}
				free(nfhp, M_NFSFH);
			} else if (recalltype == NFSLAYOUTRETURN_FSID) {
				NFSM_DISSECT(tl, uint32_t *, 2 * NFSX_HYPER);
				filesid[0] = fxdr_hyper(tl); tl += 2;
				filesid[1] = fxdr_hyper(tl); tl += 2;
				gotone = 0;
				NFSLOCKCLSTATE();
				clp = nfscl_getclntsess(sessionid);
				if (clp != NULL) {
					TAILQ_FOREACH(lyp, &clp->nfsc_layout,
					    nfsly_list) {
						if (lyp->nfsly_filesid[0] ==
						    filesid[0] &&
						    lyp->nfsly_filesid[1] ==
						    filesid[1]) {
							error =
							    nfscl_layoutrecall(
							    recalltype,
							    lyp, iomode, 0,
							    UINT64_MAX,
							    lyp->nfsly_stateid.seqid,
							    0, 0, NULL,
							    recallp);
							recallp = NULL;
							gotone = 1;
						}
					}
					if (gotone != 0)
						wakeup(clp);
					else
						error = NFSERR_NOMATCHLAYOUT;
				} else
					error = NFSERR_NOMATCHLAYOUT;
				NFSUNLOCKCLSTATE();
			} else if (recalltype == NFSLAYOUTRETURN_ALL) {
				gotone = 0;
				NFSLOCKCLSTATE();
				clp = nfscl_getclntsess(sessionid);
				if (clp != NULL) {
					TAILQ_FOREACH(lyp, &clp->nfsc_layout,
					    nfsly_list) {
						error = nfscl_layoutrecall(
						    recalltype, lyp, iomode, 0,
						    UINT64_MAX,
						    lyp->nfsly_stateid.seqid,
						    0, 0, NULL, recallp);
						recallp = NULL;
						gotone = 1;
					}
					if (gotone != 0)
						wakeup(clp);
					else
						error = NFSERR_NOMATCHLAYOUT;
				} else
					error = NFSERR_NOMATCHLAYOUT;
				NFSUNLOCKCLSTATE();
			} else
				error = NFSERR_NOMATCHLAYOUT;
			if (recallp != NULL) {
				free(recallp, M_NFSLAYRECALL);
				recallp = NULL;
			}
			break;
		case NFSV4OP_CBSEQUENCE:
			NFSM_DISSECT(tl, uint32_t *, NFSX_V4SESSIONID +
			    5 * NFSX_UNSIGNED);
			bcopy(tl, sessionid, NFSX_V4SESSIONID);
			tl += NFSX_V4SESSIONID / NFSX_UNSIGNED;
			seqid = fxdr_unsigned(uint32_t, *tl++);
			slotid = fxdr_unsigned(uint32_t, *tl++);
			highslot = fxdr_unsigned(uint32_t, *tl++);
			cachethis = *tl++;
			/* Throw away the referring call stuff. */
			clist = fxdr_unsigned(int, *tl);
			for (j = 0; j < clist; j++) {
				NFSM_DISSECT(tl, uint32_t *, NFSX_V4SESSIONID +
				    NFSX_UNSIGNED);
				tl += NFSX_V4SESSIONID / NFSX_UNSIGNED;
				rcalls = fxdr_unsigned(int, *tl);
				for (k = 0; k < rcalls; k++) {
					NFSM_DISSECT(tl, uint32_t *,
					    2 * NFSX_UNSIGNED);
				}
			}
			NFSLOCKCLSTATE();
			if (i == 0) {
				clp = nfscl_getclntsess(sessionid);
				if (clp == NULL)
					error = NFSERR_SERVERFAULT;
			} else
				error = NFSERR_SEQUENCEPOS;
			if (error == 0) {
				tsep = nfsmnt_mdssession(clp->nfsc_nmp);
				error = nfsv4_seqsession(seqid, slotid,
				    highslot, tsep->nfsess_cbslots, &rep,
				    tsep->nfsess_backslots);
			}
			NFSUNLOCKCLSTATE();
			if (error == 0 || error == NFSERR_REPLYFROMCACHE) {
				gotseq_ok = 1;
				if (rep != NULL) {
					/*
					 * Handle a reply for a retried
					 * callback.  The reply will be
					 * re-inserted in the session cache
					 * by the nfsv4_seqsess_cacherep() call
					 * after out:
					 */
					KASSERT(error == NFSERR_REPLYFROMCACHE,
					    ("cbsequence: non-NULL rep"));
					NFSCL_DEBUG(4, "Got cbretry\n");
					m_freem(nd->nd_mreq);
					nd->nd_mreq = rep;
					rep = NULL;
					goto out;
				}
				NFSM_BUILD(tl, uint32_t *,
				    NFSX_V4SESSIONID + 4 * NFSX_UNSIGNED);
				bcopy(sessionid, tl, NFSX_V4SESSIONID);
				tl += NFSX_V4SESSIONID / NFSX_UNSIGNED;
				*tl++ = txdr_unsigned(seqid);
				*tl++ = txdr_unsigned(slotid);
				*tl++ = txdr_unsigned(NFSV4_CBSLOTS - 1);
				*tl = txdr_unsigned(NFSV4_CBSLOTS - 1);
			}
			break;
		default:
			if (i == 0 && minorvers == NFSV41_MINORVERSION)
				error = NFSERR_OPNOTINSESS;
			else {
				NFSCL_DEBUG(1, "unsupp callback %d\n", op);
				error = NFSERR_NOTSUPP;
			}
			break;
		}
		if (error) {
			if (error == EBADRPC || error == NFSERR_BADXDR) {
				nd->nd_repstat = NFSERR_BADXDR;
			} else {
				nd->nd_repstat = error;
			}
			error = 0;
		}
		retops++;
		if (nd->nd_repstat) {
			*repp = nfscl_errmap(nd, minorvers);
			break;
		} else
			*repp = 0;	/* NFS4_OK */
	}
nfsmout:
	if (recallp != NULL)
		free(recallp, M_NFSLAYRECALL);
	if (error) {
		if (error == EBADRPC || error == NFSERR_BADXDR)
			nd->nd_repstat = NFSERR_BADXDR;
		else
			printf("nfsv4 comperr1=%d\n", error);
	}
	if (taglen == -1) {
		NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
		*tl++ = 0;
		*tl = 0;
	} else {
		*retopsp = txdr_unsigned(retops);
	}
	*nd->nd_errp = nfscl_errmap(nd, minorvers);
out:
	if (gotseq_ok != 0) {
		rep = m_copym(nd->nd_mreq, 0, M_COPYALL, M_WAITOK);
		NFSLOCKCLSTATE();
		clp = nfscl_getclntsess(sessionid);
		if (clp != NULL) {
			tsep = nfsmnt_mdssession(clp->nfsc_nmp);
			nfsv4_seqsess_cacherep(slotid, tsep->nfsess_cbslots,
			    NFSERR_OK, &rep);
			NFSUNLOCKCLSTATE();
		} else {
			NFSUNLOCKCLSTATE();
			m_freem(rep);
		}
	}
}

/*
 * Generate the next cbident value. Basically just increment a static value
 * and then check that it isn't already in the list, if it has wrapped around.
 */
static u_int32_t
nfscl_nextcbident(void)
{
	struct nfsclclient *clp;
	int matched;
	static u_int32_t nextcbident = 0;
	static int haswrapped = 0;

	nextcbident++;
	if (nextcbident == 0)
		haswrapped = 1;
	if (haswrapped) {
		/*
		 * Search the clientid list for one already using this cbident.
		 */
		do {
			matched = 0;
			NFSLOCKCLSTATE();
			LIST_FOREACH(clp, &nfsclhead, nfsc_list) {
				if (clp->nfsc_cbident == nextcbident) {
					matched = 1;
					break;
				}
			}
			NFSUNLOCKCLSTATE();
			if (matched == 1)
				nextcbident++;
		} while (matched);
	}
	return (nextcbident);
}

/*
 * Get the mount point related to a given cbident or session and busy it.
 */
static mount_t
nfscl_getmnt(int minorvers, uint8_t *sessionid, u_int32_t cbident,
    struct nfsclclient **clpp)
{
	struct nfsclclient *clp;
	mount_t mp;
	int error;
	struct nfsclsession *tsep;

	*clpp = NULL;
	NFSLOCKCLSTATE();
	LIST_FOREACH(clp, &nfsclhead, nfsc_list) {
		tsep = nfsmnt_mdssession(clp->nfsc_nmp);
		if (minorvers == NFSV4_MINORVERSION) {
			if (clp->nfsc_cbident == cbident)
				break;
		} else if (!NFSBCMP(tsep->nfsess_sessionid, sessionid,
		    NFSX_V4SESSIONID))
			break;
	}
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return (NULL);
	}
	mp = clp->nfsc_nmp->nm_mountp;
	vfs_ref(mp);
	NFSUNLOCKCLSTATE();
	error = vfs_busy(mp, 0);
	vfs_rel(mp);
	if (error != 0)
		return (NULL);
	*clpp = clp;
	return (mp);
}

/*
 * Get the clientid pointer related to a given cbident.
 */
static struct nfsclclient *
nfscl_getclnt(u_int32_t cbident)
{
	struct nfsclclient *clp;

	LIST_FOREACH(clp, &nfsclhead, nfsc_list)
		if (clp->nfsc_cbident == cbident)
			break;
	return (clp);
}

/*
 * Get the clientid pointer related to a given sessionid.
 */
static struct nfsclclient *
nfscl_getclntsess(uint8_t *sessionid)
{
	struct nfsclclient *clp;
	struct nfsclsession *tsep;

	LIST_FOREACH(clp, &nfsclhead, nfsc_list) {
		tsep = nfsmnt_mdssession(clp->nfsc_nmp);
		if (!NFSBCMP(tsep->nfsess_sessionid, sessionid,
		    NFSX_V4SESSIONID))
			break;
	}
	return (clp);
}

/*
 * Search for a lock conflict locally on the client. A conflict occurs if
 * - not same owner and overlapping byte range and at least one of them is
 *   a write lock or this is an unlock.
 */
static int
nfscl_localconflict(struct nfsclclient *clp, u_int8_t *fhp, int fhlen,
    struct nfscllock *nlop, u_int8_t *own, struct nfscldeleg *dp,
    struct nfscllock **lopp)
{
	struct nfsclowner *owp;
	struct nfsclopen *op;
	int ret;

	if (dp != NULL) {
		ret = nfscl_checkconflict(&dp->nfsdl_lock, nlop, own, lopp);
		if (ret)
			return (ret);
	}
	LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
		LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
			if (op->nfso_fhlen == fhlen &&
			    !NFSBCMP(op->nfso_fh, fhp, fhlen)) {
				ret = nfscl_checkconflict(&op->nfso_lock, nlop,
				    own, lopp);
				if (ret)
					return (ret);
			}
		}
	}
	return (0);
}

static int
nfscl_checkconflict(struct nfscllockownerhead *lhp, struct nfscllock *nlop,
    u_int8_t *own, struct nfscllock **lopp)
{
	struct nfscllockowner *lp;
	struct nfscllock *lop;

	LIST_FOREACH(lp, lhp, nfsl_list) {
		if (NFSBCMP(lp->nfsl_owner, own, NFSV4CL_LOCKNAMELEN)) {
			LIST_FOREACH(lop, &lp->nfsl_lock, nfslo_list) {
				if (lop->nfslo_first >= nlop->nfslo_end)
					break;
				if (lop->nfslo_end <= nlop->nfslo_first)
					continue;
				if (lop->nfslo_type == F_WRLCK ||
				    nlop->nfslo_type == F_WRLCK ||
				    nlop->nfslo_type == F_UNLCK) {
					if (lopp != NULL)
						*lopp = lop;
					return (NFSERR_DENIED);
				}
			}
		}
	}
	return (0);
}

/*
 * Check for a local conflicting lock.
 */
APPLESTATIC int
nfscl_lockt(vnode_t vp, struct nfsclclient *clp, u_int64_t off,
    u_int64_t len, struct flock *fl, NFSPROC_T *p, void *id, int flags)
{
	struct nfscllock *lop, nlck;
	struct nfscldeleg *dp;
	struct nfsnode *np;
	u_int8_t own[NFSV4CL_LOCKNAMELEN];
	int error;

	nlck.nfslo_type = fl->l_type;
	nlck.nfslo_first = off;
	if (len == NFS64BITSSET) {
		nlck.nfslo_end = NFS64BITSSET;
	} else {
		nlck.nfslo_end = off + len;
		if (nlck.nfslo_end <= nlck.nfslo_first)
			return (NFSERR_INVAL);
	}
	np = VTONFS(vp);
	nfscl_filllockowner(id, own, flags);
	NFSLOCKCLSTATE();
	dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len);
	error = nfscl_localconflict(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len,
	    &nlck, own, dp, &lop);
	if (error != 0) {
		fl->l_whence = SEEK_SET;
		fl->l_start = lop->nfslo_first;
		if (lop->nfslo_end == NFS64BITSSET)
			fl->l_len = 0;
		else
			fl->l_len = lop->nfslo_end - lop->nfslo_first;
		fl->l_pid = (pid_t)0;
		fl->l_type = lop->nfslo_type;
		error = -1;			/* no RPC required */
	} else if (dp != NULL && ((dp->nfsdl_flags & NFSCLDL_WRITE) ||
	    fl->l_type == F_RDLCK)) {
		/*
		 * The delegation ensures that there isn't a conflicting
		 * lock on the server, so return -1 to indicate an RPC
		 * isn't required.
		 */
		fl->l_type = F_UNLCK;
		error = -1;
	}
	NFSUNLOCKCLSTATE();
	return (error);
}

/*
 * Handle Recall of a delegation.
 * The clp must be exclusive locked when this is called.
 */
static int
nfscl_recalldeleg(struct nfsclclient *clp, struct nfsmount *nmp,
    struct nfscldeleg *dp, vnode_t vp, struct ucred *cred, NFSPROC_T *p,
    int called_from_renewthread)
{
	struct nfsclowner *owp, *lowp, *nowp;
	struct nfsclopen *op, *lop;
	struct nfscllockowner *lp;
	struct nfscllock *lckp;
	struct nfsnode *np;
	int error = 0, ret, gotvp = 0;

	if (vp == NULL) {
		/*
		 * First, get a vnode for the file. This is needed to do RPCs.
		 */
		ret = nfscl_ngetreopen(nmp->nm_mountp, dp->nfsdl_fh,
		    dp->nfsdl_fhlen, p, &np);
		if (ret) {
			/*
			 * File isn't open, so nothing to move over to the
			 * server.
			 */
			return (0);
		}
		vp = NFSTOV(np);
		gotvp = 1;
	} else {
		np = VTONFS(vp);
	}
	dp->nfsdl_flags &= ~NFSCLDL_MODTIMESET;

	/*
	 * Ok, if it's a write delegation, flush data to the server, so
	 * that close/open consistency is retained.
	 */
	ret = 0;
	NFSLOCKNODE(np);
	if ((dp->nfsdl_flags & NFSCLDL_WRITE) && (np->n_flag & NMODIFIED)) {
		np->n_flag |= NDELEGRECALL;
		NFSUNLOCKNODE(np);
		ret = ncl_flush(vp, MNT_WAIT, p, 1, called_from_renewthread);
		NFSLOCKNODE(np);
		np->n_flag &= ~NDELEGRECALL;
	}
	NFSINVALATTRCACHE(np);
	NFSUNLOCKNODE(np);
	if (ret == EIO && called_from_renewthread != 0) {
		/*
		 * If the flush failed with EIO for the renew thread,
		 * return now, so that the dirty buffer will be flushed
		 * later.
		 */
		if (gotvp != 0)
			vrele(vp);
		return (ret);
	}

	/*
	 * Now, for each openowner with opens issued locally, move them
	 * over to state against the server.
	 */
	LIST_FOREACH(lowp, &dp->nfsdl_owner, nfsow_list) {
		lop = LIST_FIRST(&lowp->nfsow_open);
		if (lop != NULL) {
			if (LIST_NEXT(lop, nfso_list) != NULL)
				panic("nfsdlg mult opens");
			/*
			 * Look for the same openowner against the server.
			 */
			LIST_FOREACH(owp, &clp->nfsc_owner, nfsow_list) {
				if (!NFSBCMP(lowp->nfsow_owner,
				    owp->nfsow_owner, NFSV4CL_LOCKNAMELEN)) {
					newnfs_copycred(&dp->nfsdl_cred, cred);
					ret = nfscl_moveopen(vp, clp, nmp, lop,
					    owp, dp, cred, p);
					if (ret == NFSERR_STALECLIENTID ||
					    ret == NFSERR_STALEDONTRECOVER ||
					    ret == NFSERR_BADSESSION) {
						if (gotvp)
							vrele(vp);
						return (ret);
					}
					if (ret) {
						nfscl_freeopen(lop, 1);
						if (!error)
							error = ret;
					}
					break;
				}
			}

			/*
			 * If no openowner found, create one and get an open
			 * for it.
			 */
			if (owp == NULL) {
				nowp = malloc(
				    sizeof (struct nfsclowner), M_NFSCLOWNER,
				    M_WAITOK);
				nfscl_newopen(clp, NULL, &owp, &nowp, &op, 
				    NULL, lowp->nfsow_owner, dp->nfsdl_fh,
				    dp->nfsdl_fhlen, NULL, NULL);
				newnfs_copycred(&dp->nfsdl_cred, cred);
				ret = nfscl_moveopen(vp, clp, nmp, lop,
				    owp, dp, cred, p);
				if (ret) {
					nfscl_freeopenowner(owp, 0);
					if (ret == NFSERR_STALECLIENTID ||
					    ret == NFSERR_STALEDONTRECOVER ||
					    ret == NFSERR_BADSESSION) {
						if (gotvp)
							vrele(vp);
						return (ret);
					}
					if (ret) {
						nfscl_freeopen(lop, 1);
						if (!error)
							error = ret;
					}
				}
			}
		}
	}

	/*
	 * Now, get byte range locks for any locks done locally.
	 */
	LIST_FOREACH(lp, &dp->nfsdl_lock, nfsl_list) {
		LIST_FOREACH(lckp, &lp->nfsl_lock, nfslo_list) {
			newnfs_copycred(&dp->nfsdl_cred, cred);
			ret = nfscl_relock(vp, clp, nmp, lp, lckp, cred, p);
			if (ret == NFSERR_STALESTATEID ||
			    ret == NFSERR_STALEDONTRECOVER ||
			    ret == NFSERR_STALECLIENTID ||
			    ret == NFSERR_BADSESSION) {
				if (gotvp)
					vrele(vp);
				return (ret);
			}
			if (ret && !error)
				error = ret;
		}
	}
	if (gotvp)
		vrele(vp);
	return (error);
}

/*
 * Move a locally issued open over to an owner on the state list.
 * SIDE EFFECT: If it needs to sleep (do an rpc), it unlocks clstate and
 * returns with it unlocked.
 */
static int
nfscl_moveopen(vnode_t vp, struct nfsclclient *clp, struct nfsmount *nmp,
    struct nfsclopen *lop, struct nfsclowner *owp, struct nfscldeleg *dp,
    struct ucred *cred, NFSPROC_T *p)
{
	struct nfsclopen *op, *nop;
	struct nfscldeleg *ndp;
	struct nfsnode *np;
	int error = 0, newone;

	/*
	 * First, look for an appropriate open, If found, just increment the
	 * opencnt in it.
	 */
	LIST_FOREACH(op, &owp->nfsow_open, nfso_list) {
		if ((op->nfso_mode & lop->nfso_mode) == lop->nfso_mode &&
		    op->nfso_fhlen == lop->nfso_fhlen &&
		    !NFSBCMP(op->nfso_fh, lop->nfso_fh, op->nfso_fhlen)) {
			op->nfso_opencnt += lop->nfso_opencnt;
			nfscl_freeopen(lop, 1);
			return (0);
		}
	}

	/* No appropriate open, so we have to do one against the server. */
	np = VTONFS(vp);
	nop = malloc(sizeof (struct nfsclopen) +
	    lop->nfso_fhlen - 1, M_NFSCLOPEN, M_WAITOK);
	newone = 0;
	nfscl_newopen(clp, NULL, &owp, NULL, &op, &nop, owp->nfsow_owner,
	    lop->nfso_fh, lop->nfso_fhlen, cred, &newone);
	ndp = dp;
	error = nfscl_tryopen(nmp, vp, np->n_v4->n4_data, np->n_v4->n4_fhlen,
	    lop->nfso_fh, lop->nfso_fhlen, lop->nfso_mode, op,
	    NFS4NODENAME(np->n_v4), np->n_v4->n4_namelen, &ndp, 0, 0, cred, p);
	if (error) {
		if (newone)
			nfscl_freeopen(op, 0);
	} else {
		op->nfso_mode |= lop->nfso_mode;
		op->nfso_opencnt += lop->nfso_opencnt;
		nfscl_freeopen(lop, 1);
	}
	if (nop != NULL)
		free(nop, M_NFSCLOPEN);
	if (ndp != NULL) {
		/*
		 * What should I do with the returned delegation, since the
		 * delegation is being recalled? For now, just printf and
		 * through it away.
		 */
		printf("Moveopen returned deleg\n");
		free(ndp, M_NFSCLDELEG);
	}
	return (error);
}

/*
 * Recall all delegations on this client.
 */
static void
nfscl_totalrecall(struct nfsclclient *clp)
{
	struct nfscldeleg *dp;

	TAILQ_FOREACH(dp, &clp->nfsc_deleg, nfsdl_list) {
		if ((dp->nfsdl_flags & NFSCLDL_DELEGRET) == 0)
			dp->nfsdl_flags |= NFSCLDL_RECALL;
	}
}

/*
 * Relock byte ranges. Called for delegation recall and state expiry.
 */
static int
nfscl_relock(vnode_t vp, struct nfsclclient *clp, struct nfsmount *nmp,
    struct nfscllockowner *lp, struct nfscllock *lop, struct ucred *cred,
    NFSPROC_T *p)
{
	struct nfscllockowner *nlp;
	struct nfsfh *nfhp;
	u_int64_t off, len;
	int error, newone, donelocally;

	off = lop->nfslo_first;
	len = lop->nfslo_end - lop->nfslo_first;
	error = nfscl_getbytelock(vp, off, len, lop->nfslo_type, cred, p,
	    clp, 1, NULL, lp->nfsl_lockflags, lp->nfsl_owner,
	    lp->nfsl_openowner, &nlp, &newone, &donelocally);
	if (error || donelocally)
		return (error);
	nfhp = VTONFS(vp)->n_fhp;
	error = nfscl_trylock(nmp, vp, nfhp->nfh_fh,
	    nfhp->nfh_len, nlp, newone, 0, off,
	    len, lop->nfslo_type, cred, p);
	if (error)
		nfscl_freelockowner(nlp, 0);
	return (error);
}

/*
 * Called to re-open a file. Basically get a vnode for the file handle
 * and then call nfsrpc_openrpc() to do the rest.
 */
static int
nfsrpc_reopen(struct nfsmount *nmp, u_int8_t *fhp, int fhlen,
    u_int32_t mode, struct nfsclopen *op, struct nfscldeleg **dpp,
    struct ucred *cred, NFSPROC_T *p)
{
	struct nfsnode *np;
	vnode_t vp;
	int error;

	error = nfscl_ngetreopen(nmp->nm_mountp, fhp, fhlen, p, &np);
	if (error)
		return (error);
	vp = NFSTOV(np);
	if (np->n_v4 != NULL) {
		error = nfscl_tryopen(nmp, vp, np->n_v4->n4_data,
		    np->n_v4->n4_fhlen, fhp, fhlen, mode, op,
		    NFS4NODENAME(np->n_v4), np->n_v4->n4_namelen, dpp, 0, 0,
		    cred, p);
	} else {
		error = EINVAL;
	}
	vrele(vp);
	return (error);
}

/*
 * Try an open against the server. Just call nfsrpc_openrpc(), retrying while
 * NFSERR_DELAY. Also, try system credentials, if the passed in credentials
 * fail.
 */
static int
nfscl_tryopen(struct nfsmount *nmp, vnode_t vp, u_int8_t *fhp, int fhlen,
    u_int8_t *newfhp, int newfhlen, u_int32_t mode, struct nfsclopen *op,
    u_int8_t *name, int namelen, struct nfscldeleg **ndpp,
    int reclaim, u_int32_t delegtype, struct ucred *cred, NFSPROC_T *p)
{
	int error;

	do {
		error = nfsrpc_openrpc(nmp, vp, fhp, fhlen, newfhp, newfhlen,
		    mode, op, name, namelen, ndpp, reclaim, delegtype, cred, p,
		    0, 0);
		if (error == NFSERR_DELAY)
			(void) nfs_catnap(PZERO, error, "nfstryop");
	} while (error == NFSERR_DELAY);
	if (error == EAUTH || error == EACCES) {
		/* Try again using system credentials */
		newnfs_setroot(cred);
		do {
		    error = nfsrpc_openrpc(nmp, vp, fhp, fhlen, newfhp,
			newfhlen, mode, op, name, namelen, ndpp, reclaim,
			delegtype, cred, p, 1, 0);
		    if (error == NFSERR_DELAY)
			(void) nfs_catnap(PZERO, error, "nfstryop");
		} while (error == NFSERR_DELAY);
	}
	return (error);
}

/*
 * Try a byte range lock. Just loop on nfsrpc_lock() while it returns
 * NFSERR_DELAY. Also, retry with system credentials, if the provided
 * cred don't work.
 */
static int
nfscl_trylock(struct nfsmount *nmp, vnode_t vp, u_int8_t *fhp,
    int fhlen, struct nfscllockowner *nlp, int newone, int reclaim,
    u_int64_t off, u_int64_t len, short type, struct ucred *cred, NFSPROC_T *p)
{
	struct nfsrv_descript nfsd, *nd = &nfsd;
	int error;

	do {
		error = nfsrpc_lock(nd, nmp, vp, fhp, fhlen, nlp, newone,
		    reclaim, off, len, type, cred, p, 0);
		if (!error && nd->nd_repstat == NFSERR_DELAY)
			(void) nfs_catnap(PZERO, (int)nd->nd_repstat,
			    "nfstrylck");
	} while (!error && nd->nd_repstat == NFSERR_DELAY);
	if (!error)
		error = nd->nd_repstat;
	if (error == EAUTH || error == EACCES) {
		/* Try again using root credentials */
		newnfs_setroot(cred);
		do {
			error = nfsrpc_lock(nd, nmp, vp, fhp, fhlen, nlp,
			    newone, reclaim, off, len, type, cred, p, 1);
			if (!error && nd->nd_repstat == NFSERR_DELAY)
				(void) nfs_catnap(PZERO, (int)nd->nd_repstat,
				    "nfstrylck");
		} while (!error && nd->nd_repstat == NFSERR_DELAY);
		if (!error)
			error = nd->nd_repstat;
	}
	return (error);
}

/*
 * Try a delegreturn against the server. Just call nfsrpc_delegreturn(),
 * retrying while NFSERR_DELAY. Also, try system credentials, if the passed in
 * credentials fail.
 */
static int
nfscl_trydelegreturn(struct nfscldeleg *dp, struct ucred *cred,
    struct nfsmount *nmp, NFSPROC_T *p)
{
	int error;

	do {
		error = nfsrpc_delegreturn(dp, cred, nmp, p, 0);
		if (error == NFSERR_DELAY)
			(void) nfs_catnap(PZERO, error, "nfstrydp");
	} while (error == NFSERR_DELAY);
	if (error == EAUTH || error == EACCES) {
		/* Try again using system credentials */
		newnfs_setroot(cred);
		do {
			error = nfsrpc_delegreturn(dp, cred, nmp, p, 1);
			if (error == NFSERR_DELAY)
				(void) nfs_catnap(PZERO, error, "nfstrydp");
		} while (error == NFSERR_DELAY);
	}
	return (error);
}

/*
 * Try a close against the server. Just call nfsrpc_closerpc(),
 * retrying while NFSERR_DELAY. Also, try system credentials, if the passed in
 * credentials fail.
 */
APPLESTATIC int
nfscl_tryclose(struct nfsclopen *op, struct ucred *cred,
    struct nfsmount *nmp, NFSPROC_T *p)
{
	struct nfsrv_descript nfsd, *nd = &nfsd;
	int error;

	do {
		error = nfsrpc_closerpc(nd, nmp, op, cred, p, 0);
		if (error == NFSERR_DELAY)
			(void) nfs_catnap(PZERO, error, "nfstrycl");
	} while (error == NFSERR_DELAY);
	if (error == EAUTH || error == EACCES) {
		/* Try again using system credentials */
		newnfs_setroot(cred);
		do {
			error = nfsrpc_closerpc(nd, nmp, op, cred, p, 1);
			if (error == NFSERR_DELAY)
				(void) nfs_catnap(PZERO, error, "nfstrycl");
		} while (error == NFSERR_DELAY);
	}
	return (error);
}

/*
 * Decide if a delegation on a file permits close without flushing writes
 * to the server. This might be a big performance win in some environments.
 * (Not useful until the client does caching on local stable storage.)
 */
APPLESTATIC int
nfscl_mustflush(vnode_t vp)
{
	struct nfsclclient *clp;
	struct nfscldeleg *dp;
	struct nfsnode *np;
	struct nfsmount *nmp;

	np = VTONFS(vp);
	nmp = VFSTONFS(vnode_mount(vp));
	if (!NFSHASNFSV4(nmp))
		return (1);
	NFSLOCKCLSTATE();
	clp = nfscl_findcl(nmp);
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return (1);
	}
	dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len);
	if (dp != NULL && (dp->nfsdl_flags &
	    (NFSCLDL_WRITE | NFSCLDL_RECALL | NFSCLDL_DELEGRET)) ==
	     NFSCLDL_WRITE &&
	    (dp->nfsdl_sizelimit >= np->n_size ||
	     !NFSHASSTRICT3530(nmp))) {
		NFSUNLOCKCLSTATE();
		return (0);
	}
	NFSUNLOCKCLSTATE();
	return (1);
}

/*
 * See if a (write) delegation exists for this file.
 */
APPLESTATIC int
nfscl_nodeleg(vnode_t vp, int writedeleg)
{
	struct nfsclclient *clp;
	struct nfscldeleg *dp;
	struct nfsnode *np;
	struct nfsmount *nmp;

	np = VTONFS(vp);
	nmp = VFSTONFS(vnode_mount(vp));
	if (!NFSHASNFSV4(nmp))
		return (1);
	NFSLOCKCLSTATE();
	clp = nfscl_findcl(nmp);
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return (1);
	}
	dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len);
	if (dp != NULL &&
	    (dp->nfsdl_flags & (NFSCLDL_RECALL | NFSCLDL_DELEGRET)) == 0 &&
	    (writedeleg == 0 || (dp->nfsdl_flags & NFSCLDL_WRITE) ==
	     NFSCLDL_WRITE)) {
		NFSUNLOCKCLSTATE();
		return (0);
	}
	NFSUNLOCKCLSTATE();
	return (1);
}

/*
 * Look for an associated delegation that should be DelegReturned.
 */
APPLESTATIC int
nfscl_removedeleg(vnode_t vp, NFSPROC_T *p, nfsv4stateid_t *stp)
{
	struct nfsclclient *clp;
	struct nfscldeleg *dp;
	struct nfsclowner *owp;
	struct nfscllockowner *lp;
	struct nfsmount *nmp;
	struct ucred *cred;
	struct nfsnode *np;
	int igotlock = 0, triedrecall = 0, needsrecall, retcnt = 0, islept;

	nmp = VFSTONFS(vnode_mount(vp));
	np = VTONFS(vp);
	NFSLOCKCLSTATE();
	/*
	 * Loop around waiting for:
	 * - outstanding I/O operations on delegations to complete
	 * - for a delegation on vp that has state, lock the client and
	 *   do a recall
	 * - return delegation with no state
	 */
	while (1) {
		clp = nfscl_findcl(nmp);
		if (clp == NULL) {
			NFSUNLOCKCLSTATE();
			return (retcnt);
		}
		dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh,
		    np->n_fhp->nfh_len);
		if (dp != NULL) {
		    /*
		     * Wait for outstanding I/O ops to be done.
		     */
		    if (dp->nfsdl_rwlock.nfslock_usecnt > 0) {
			if (igotlock) {
			    nfsv4_unlock(&clp->nfsc_lock, 0);
			    igotlock = 0;
			}
			dp->nfsdl_rwlock.nfslock_lock |= NFSV4LOCK_WANTED;
			(void) nfsmsleep(&dp->nfsdl_rwlock,
			    NFSCLSTATEMUTEXPTR, PZERO, "nfscld", NULL);
			continue;
		    }
		    needsrecall = 0;
		    LIST_FOREACH(owp, &dp->nfsdl_owner, nfsow_list) {
			if (!LIST_EMPTY(&owp->nfsow_open)) {
			    needsrecall = 1;
			    break;
			}
		    }
		    if (!needsrecall) {
			LIST_FOREACH(lp, &dp->nfsdl_lock, nfsl_list) {
			    if (!LIST_EMPTY(&lp->nfsl_lock)) {
				needsrecall = 1;
				break;
			    }
			}
		    }
		    if (needsrecall && !triedrecall) {
			dp->nfsdl_flags |= NFSCLDL_DELEGRET;
			islept = 0;
			while (!igotlock) {
			    igotlock = nfsv4_lock(&clp->nfsc_lock, 1,
				&islept, NFSCLSTATEMUTEXPTR, NULL);
			    if (islept)
				break;
			}
			if (islept)
			    continue;
			NFSUNLOCKCLSTATE();
			cred = newnfs_getcred();
			newnfs_copycred(&dp->nfsdl_cred, cred);
			(void) nfscl_recalldeleg(clp, nmp, dp, vp, cred, p, 0);
			NFSFREECRED(cred);
			triedrecall = 1;
			NFSLOCKCLSTATE();
			nfsv4_unlock(&clp->nfsc_lock, 0);
			igotlock = 0;
			continue;
		    }
		    *stp = dp->nfsdl_stateid;
		    retcnt = 1;
		    nfscl_cleandeleg(dp);
		    nfscl_freedeleg(&clp->nfsc_deleg, dp);
		}
		if (igotlock)
		    nfsv4_unlock(&clp->nfsc_lock, 0);
		NFSUNLOCKCLSTATE();
		return (retcnt);
	}
}

/*
 * Look for associated delegation(s) that should be DelegReturned.
 */
APPLESTATIC int
nfscl_renamedeleg(vnode_t fvp, nfsv4stateid_t *fstp, int *gotfdp, vnode_t tvp,
    nfsv4stateid_t *tstp, int *gottdp, NFSPROC_T *p)
{
	struct nfsclclient *clp;
	struct nfscldeleg *dp;
	struct nfsclowner *owp;
	struct nfscllockowner *lp;
	struct nfsmount *nmp;
	struct ucred *cred;
	struct nfsnode *np;
	int igotlock = 0, triedrecall = 0, needsrecall, retcnt = 0, islept;

	nmp = VFSTONFS(vnode_mount(fvp));
	*gotfdp = 0;
	*gottdp = 0;
	NFSLOCKCLSTATE();
	/*
	 * Loop around waiting for:
	 * - outstanding I/O operations on delegations to complete
	 * - for a delegation on fvp that has state, lock the client and
	 *   do a recall
	 * - return delegation(s) with no state.
	 */
	while (1) {
		clp = nfscl_findcl(nmp);
		if (clp == NULL) {
			NFSUNLOCKCLSTATE();
			return (retcnt);
		}
		np = VTONFS(fvp);
		dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh,
		    np->n_fhp->nfh_len);
		if (dp != NULL && *gotfdp == 0) {
		    /*
		     * Wait for outstanding I/O ops to be done.
		     */
		    if (dp->nfsdl_rwlock.nfslock_usecnt > 0) {
			if (igotlock) {
			    nfsv4_unlock(&clp->nfsc_lock, 0);
			    igotlock = 0;
			}
			dp->nfsdl_rwlock.nfslock_lock |= NFSV4LOCK_WANTED;
			(void) nfsmsleep(&dp->nfsdl_rwlock,
			    NFSCLSTATEMUTEXPTR, PZERO, "nfscld", NULL);
			continue;
		    }
		    needsrecall = 0;
		    LIST_FOREACH(owp, &dp->nfsdl_owner, nfsow_list) {
			if (!LIST_EMPTY(&owp->nfsow_open)) {
			    needsrecall = 1;
			    break;
			}
		    }
		    if (!needsrecall) {
			LIST_FOREACH(lp, &dp->nfsdl_lock, nfsl_list) {
			    if (!LIST_EMPTY(&lp->nfsl_lock)) {
				needsrecall = 1;
				break;
			    }
			}
		    }
		    if (needsrecall && !triedrecall) {
			dp->nfsdl_flags |= NFSCLDL_DELEGRET;
			islept = 0;
			while (!igotlock) {
			    igotlock = nfsv4_lock(&clp->nfsc_lock, 1,
				&islept, NFSCLSTATEMUTEXPTR, NULL);
			    if (islept)
				break;
			}
			if (islept)
			    continue;
			NFSUNLOCKCLSTATE();
			cred = newnfs_getcred();
			newnfs_copycred(&dp->nfsdl_cred, cred);
			(void) nfscl_recalldeleg(clp, nmp, dp, fvp, cred, p, 0);
			NFSFREECRED(cred);
			triedrecall = 1;
			NFSLOCKCLSTATE();
			nfsv4_unlock(&clp->nfsc_lock, 0);
			igotlock = 0;
			continue;
		    }
		    *fstp = dp->nfsdl_stateid;
		    retcnt++;
		    *gotfdp = 1;
		    nfscl_cleandeleg(dp);
		    nfscl_freedeleg(&clp->nfsc_deleg, dp);
		}
		if (igotlock) {
		    nfsv4_unlock(&clp->nfsc_lock, 0);
		    igotlock = 0;
		}
		if (tvp != NULL) {
		    np = VTONFS(tvp);
		    dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh,
			np->n_fhp->nfh_len);
		    if (dp != NULL && *gottdp == 0) {
			/*
			 * Wait for outstanding I/O ops to be done.
			 */
			if (dp->nfsdl_rwlock.nfslock_usecnt > 0) {
			    dp->nfsdl_rwlock.nfslock_lock |= NFSV4LOCK_WANTED;
			    (void) nfsmsleep(&dp->nfsdl_rwlock,
				NFSCLSTATEMUTEXPTR, PZERO, "nfscld", NULL);
			    continue;
			}
			LIST_FOREACH(owp, &dp->nfsdl_owner, nfsow_list) {
			    if (!LIST_EMPTY(&owp->nfsow_open)) {
				NFSUNLOCKCLSTATE();
				return (retcnt);
			    }
			}
			LIST_FOREACH(lp, &dp->nfsdl_lock, nfsl_list) {
			    if (!LIST_EMPTY(&lp->nfsl_lock)) {
				NFSUNLOCKCLSTATE();
				return (retcnt);
			    }
			}
			*tstp = dp->nfsdl_stateid;
			retcnt++;
			*gottdp = 1;
			nfscl_cleandeleg(dp);
			nfscl_freedeleg(&clp->nfsc_deleg, dp);
		    }
		}
		NFSUNLOCKCLSTATE();
		return (retcnt);
	}
}

/*
 * Get a reference on the clientid associated with the mount point.
 * Return 1 if success, 0 otherwise.
 */
APPLESTATIC int
nfscl_getref(struct nfsmount *nmp)
{
	struct nfsclclient *clp;

	NFSLOCKCLSTATE();
	clp = nfscl_findcl(nmp);
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return (0);
	}
	nfsv4_getref(&clp->nfsc_lock, NULL, NFSCLSTATEMUTEXPTR, NULL);
	NFSUNLOCKCLSTATE();
	return (1);
}

/*
 * Release a reference on a clientid acquired with the above call.
 */
APPLESTATIC void
nfscl_relref(struct nfsmount *nmp)
{
	struct nfsclclient *clp;

	NFSLOCKCLSTATE();
	clp = nfscl_findcl(nmp);
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return;
	}
	nfsv4_relref(&clp->nfsc_lock);
	NFSUNLOCKCLSTATE();
}

/*
 * Save the size attribute in the delegation, since the nfsnode
 * is going away.
 */
APPLESTATIC void
nfscl_reclaimnode(vnode_t vp)
{
	struct nfsclclient *clp;
	struct nfscldeleg *dp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp;

	nmp = VFSTONFS(vnode_mount(vp));
	if (!NFSHASNFSV4(nmp))
		return;
	NFSLOCKCLSTATE();
	clp = nfscl_findcl(nmp);
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return;
	}
	dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len);
	if (dp != NULL && (dp->nfsdl_flags & NFSCLDL_WRITE))
		dp->nfsdl_size = np->n_size;
	NFSUNLOCKCLSTATE();
}

/*
 * Get the saved size attribute in the delegation, since it is a
 * newly allocated nfsnode.
 */
APPLESTATIC void
nfscl_newnode(vnode_t vp)
{
	struct nfsclclient *clp;
	struct nfscldeleg *dp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp;

	nmp = VFSTONFS(vnode_mount(vp));
	if (!NFSHASNFSV4(nmp))
		return;
	NFSLOCKCLSTATE();
	clp = nfscl_findcl(nmp);
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return;
	}
	dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len);
	if (dp != NULL && (dp->nfsdl_flags & NFSCLDL_WRITE))
		np->n_size = dp->nfsdl_size;
	NFSUNLOCKCLSTATE();
}

/*
 * If there is a valid write delegation for this file, set the modtime
 * to the local clock time.
 */
APPLESTATIC void
nfscl_delegmodtime(vnode_t vp)
{
	struct nfsclclient *clp;
	struct nfscldeleg *dp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp;

	nmp = VFSTONFS(vnode_mount(vp));
	if (!NFSHASNFSV4(nmp))
		return;
	NFSLOCKCLSTATE();
	clp = nfscl_findcl(nmp);
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return;
	}
	dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len);
	if (dp != NULL && (dp->nfsdl_flags & NFSCLDL_WRITE)) {
		nanotime(&dp->nfsdl_modtime);
		dp->nfsdl_flags |= NFSCLDL_MODTIMESET;
	}
	NFSUNLOCKCLSTATE();
}

/*
 * If there is a valid write delegation for this file with a modtime set,
 * put that modtime in mtime.
 */
APPLESTATIC void
nfscl_deleggetmodtime(vnode_t vp, struct timespec *mtime)
{
	struct nfsclclient *clp;
	struct nfscldeleg *dp;
	struct nfsnode *np = VTONFS(vp);
	struct nfsmount *nmp;

	nmp = VFSTONFS(vnode_mount(vp));
	if (!NFSHASNFSV4(nmp))
		return;
	NFSLOCKCLSTATE();
	clp = nfscl_findcl(nmp);
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return;
	}
	dp = nfscl_finddeleg(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len);
	if (dp != NULL &&
	    (dp->nfsdl_flags & (NFSCLDL_WRITE | NFSCLDL_MODTIMESET)) ==
	    (NFSCLDL_WRITE | NFSCLDL_MODTIMESET))
		*mtime = dp->nfsdl_modtime;
	NFSUNLOCKCLSTATE();
}

static int
nfscl_errmap(struct nfsrv_descript *nd, u_int32_t minorvers)
{
	short *defaulterrp, *errp;

	if (!nd->nd_repstat)
		return (0);
	if (nd->nd_procnum == NFSPROC_NOOP)
		return (txdr_unsigned(nd->nd_repstat & 0xffff));
	if (nd->nd_repstat == EBADRPC)
		return (txdr_unsigned(NFSERR_BADXDR));
	if (nd->nd_repstat == NFSERR_MINORVERMISMATCH ||
	    nd->nd_repstat == NFSERR_OPILLEGAL)
		return (txdr_unsigned(nd->nd_repstat));
	if (nd->nd_repstat >= NFSERR_BADIOMODE && nd->nd_repstat < 20000 &&
	    minorvers > NFSV4_MINORVERSION) {
		/* NFSv4.n error. */
		return (txdr_unsigned(nd->nd_repstat));
	}
	if (nd->nd_procnum < NFSV4OP_CBNOPS)
		errp = defaulterrp = nfscl_cberrmap[nd->nd_procnum];
	else
		return (txdr_unsigned(nd->nd_repstat));
	while (*++errp)
		if (*errp == (short)nd->nd_repstat)
			return (txdr_unsigned(nd->nd_repstat));
	return (txdr_unsigned(*defaulterrp));
}

/*
 * Called to find/add a layout to a client.
 * This function returns the layout with a refcnt (shared lock) upon
 * success (returns 0) or with no lock/refcnt on the layout when an
 * error is returned.
 * If a layout is passed in via lypp, it is locked (exclusively locked).
 */
APPLESTATIC int
nfscl_layout(struct nfsmount *nmp, vnode_t vp, u_int8_t *fhp, int fhlen,
    nfsv4stateid_t *stateidp, int layouttype, int retonclose,
    struct nfsclflayouthead *fhlp, struct nfscllayout **lypp,
    struct ucred *cred, NFSPROC_T *p)
{
	struct nfsclclient *clp;
	struct nfscllayout *lyp, *tlyp;
	struct nfsclflayout *flp;
	struct nfsnode *np = VTONFS(vp);
	mount_t mp;
	int layout_passed_in;

	mp = nmp->nm_mountp;
	layout_passed_in = 1;
	tlyp = NULL;
	lyp = *lypp;
	if (lyp == NULL) {
		layout_passed_in = 0;
		tlyp = malloc(sizeof(*tlyp) + fhlen - 1, M_NFSLAYOUT,
		    M_WAITOK | M_ZERO);
	}

	NFSLOCKCLSTATE();
	clp = nmp->nm_clp;
	if (clp == NULL) {
		if (layout_passed_in != 0)
			nfsv4_unlock(&lyp->nfsly_lock, 0);
		NFSUNLOCKCLSTATE();
		if (tlyp != NULL)
			free(tlyp, M_NFSLAYOUT);
		return (EPERM);
	}
	if (lyp == NULL) {
		/*
		 * Although no lyp was passed in, another thread might have
		 * allocated one. If one is found, just increment it's ref
		 * count and return it.
		 */
		lyp = nfscl_findlayout(clp, fhp, fhlen);
		if (lyp == NULL) {
			lyp = tlyp;
			tlyp = NULL;
			lyp->nfsly_stateid.seqid = stateidp->seqid;
			lyp->nfsly_stateid.other[0] = stateidp->other[0];
			lyp->nfsly_stateid.other[1] = stateidp->other[1];
			lyp->nfsly_stateid.other[2] = stateidp->other[2];
			lyp->nfsly_lastbyte = 0;
			LIST_INIT(&lyp->nfsly_flayread);
			LIST_INIT(&lyp->nfsly_flayrw);
			LIST_INIT(&lyp->nfsly_recall);
			lyp->nfsly_filesid[0] = np->n_vattr.na_filesid[0];
			lyp->nfsly_filesid[1] = np->n_vattr.na_filesid[1];
			lyp->nfsly_clp = clp;
			if (layouttype == NFSLAYOUT_FLEXFILE)
				lyp->nfsly_flags = NFSLY_FLEXFILE;
			else
				lyp->nfsly_flags = NFSLY_FILES;
			if (retonclose != 0)
				lyp->nfsly_flags |= NFSLY_RETONCLOSE;
			lyp->nfsly_fhlen = fhlen;
			NFSBCOPY(fhp, lyp->nfsly_fh, fhlen);
			TAILQ_INSERT_HEAD(&clp->nfsc_layout, lyp, nfsly_list);
			LIST_INSERT_HEAD(NFSCLLAYOUTHASH(clp, fhp, fhlen), lyp,
			    nfsly_hash);
			lyp->nfsly_timestamp = NFSD_MONOSEC + 120;
			nfscl_layoutcnt++;
		} else {
			if (retonclose != 0)
				lyp->nfsly_flags |= NFSLY_RETONCLOSE;
			TAILQ_REMOVE(&clp->nfsc_layout, lyp, nfsly_list);
			TAILQ_INSERT_HEAD(&clp->nfsc_layout, lyp, nfsly_list);
			lyp->nfsly_timestamp = NFSD_MONOSEC + 120;
		}
		nfsv4_getref(&lyp->nfsly_lock, NULL, NFSCLSTATEMUTEXPTR, mp);
		if (NFSCL_FORCEDISM(mp)) {
			NFSUNLOCKCLSTATE();
			if (tlyp != NULL)
				free(tlyp, M_NFSLAYOUT);
			return (EPERM);
		}
		*lypp = lyp;
	} else
		lyp->nfsly_stateid.seqid = stateidp->seqid;

	/* Merge the new list of File Layouts into the list. */
	flp = LIST_FIRST(fhlp);
	if (flp != NULL) {
		if (flp->nfsfl_iomode == NFSLAYOUTIOMODE_READ)
			nfscl_mergeflayouts(&lyp->nfsly_flayread, fhlp);
		else
			nfscl_mergeflayouts(&lyp->nfsly_flayrw, fhlp);
	}
	if (layout_passed_in != 0)
		nfsv4_unlock(&lyp->nfsly_lock, 1);
	NFSUNLOCKCLSTATE();
	if (tlyp != NULL)
		free(tlyp, M_NFSLAYOUT);
	return (0);
}

/*
 * Search for a layout by MDS file handle.
 * If one is found, it is returned with a refcnt (shared lock) iff
 * retflpp returned non-NULL and locked (exclusive locked) iff retflpp is
 * returned NULL.
 */
struct nfscllayout *
nfscl_getlayout(struct nfsclclient *clp, uint8_t *fhp, int fhlen,
    uint64_t off, struct nfsclflayout **retflpp, int *recalledp)
{
	struct nfscllayout *lyp;
	mount_t mp;
	int error, igotlock;

	mp = clp->nfsc_nmp->nm_mountp;
	*recalledp = 0;
	*retflpp = NULL;
	NFSLOCKCLSTATE();
	lyp = nfscl_findlayout(clp, fhp, fhlen);
	if (lyp != NULL) {
		if ((lyp->nfsly_flags & NFSLY_RECALL) == 0) {
			TAILQ_REMOVE(&clp->nfsc_layout, lyp, nfsly_list);
			TAILQ_INSERT_HEAD(&clp->nfsc_layout, lyp, nfsly_list);
			lyp->nfsly_timestamp = NFSD_MONOSEC + 120;
			error = nfscl_findlayoutforio(lyp, off,
			    NFSV4OPEN_ACCESSREAD, retflpp);
			if (error == 0)
				nfsv4_getref(&lyp->nfsly_lock, NULL,
				    NFSCLSTATEMUTEXPTR, mp);
			else {
				do {
					igotlock = nfsv4_lock(&lyp->nfsly_lock,
					    1, NULL, NFSCLSTATEMUTEXPTR, mp);
				} while (igotlock == 0 && !NFSCL_FORCEDISM(mp));
				*retflpp = NULL;
			}
			if (NFSCL_FORCEDISM(mp)) {
				lyp = NULL;
				*recalledp = 1;
			}
		} else {
			lyp = NULL;
			*recalledp = 1;
		}
	}
	NFSUNLOCKCLSTATE();
	return (lyp);
}

/*
 * Search for a layout by MDS file handle. If one is found, mark in to be
 * recalled, if it already marked "return on close".
 */
static void
nfscl_retoncloselayout(vnode_t vp, struct nfsclclient *clp, uint8_t *fhp,
    int fhlen, struct nfsclrecalllayout **recallpp)
{
	struct nfscllayout *lyp;
	uint32_t iomode;

	if (vp->v_type != VREG || !NFSHASPNFS(VFSTONFS(vnode_mount(vp))) ||
	    nfscl_enablecallb == 0 || nfs_numnfscbd == 0 ||
	    (VTONFS(vp)->n_flag & NNOLAYOUT) != 0)
		return;
	lyp = nfscl_findlayout(clp, fhp, fhlen);
	if (lyp != NULL && (lyp->nfsly_flags & (NFSLY_RETONCLOSE |
	    NFSLY_RECALL)) == NFSLY_RETONCLOSE) {
		iomode = 0;
		if (!LIST_EMPTY(&lyp->nfsly_flayread))
			iomode |= NFSLAYOUTIOMODE_READ;
		if (!LIST_EMPTY(&lyp->nfsly_flayrw))
			iomode |= NFSLAYOUTIOMODE_RW;
		(void)nfscl_layoutrecall(NFSLAYOUTRETURN_FILE, lyp, iomode,
		    0, UINT64_MAX, lyp->nfsly_stateid.seqid, 0, 0, NULL,
		    *recallpp);
		NFSCL_DEBUG(4, "retoncls recall iomode=%d\n", iomode);
		*recallpp = NULL;
	}
}

/*
 * Mark the layout to be recalled and with an error.
 * Also, disable the dsp from further use.
 */
void
nfscl_dserr(uint32_t op, uint32_t stat, struct nfscldevinfo *dp,
    struct nfscllayout *lyp, struct nfsclds *dsp)
{
	struct nfsclrecalllayout *recallp;
	uint32_t iomode;

	printf("DS being disabled, error=%d\n", stat);
	/* Set up the return of the layout. */
	recallp = malloc(sizeof(*recallp), M_NFSLAYRECALL, M_WAITOK);
	iomode = 0;
	NFSLOCKCLSTATE();
	if ((lyp->nfsly_flags & NFSLY_RECALL) == 0) {
		if (!LIST_EMPTY(&lyp->nfsly_flayread))
			iomode |= NFSLAYOUTIOMODE_READ;
		if (!LIST_EMPTY(&lyp->nfsly_flayrw))
			iomode |= NFSLAYOUTIOMODE_RW;
		(void)nfscl_layoutrecall(NFSLAYOUTRETURN_FILE, lyp, iomode,
		    0, UINT64_MAX, lyp->nfsly_stateid.seqid, stat, op,
		    dp->nfsdi_deviceid, recallp);
		NFSUNLOCKCLSTATE();
		NFSCL_DEBUG(4, "nfscl_dserr recall iomode=%d\n", iomode);
	} else {
		NFSUNLOCKCLSTATE();
		free(recallp, M_NFSLAYRECALL);
	}

	/* And shut the TCP connection down. */
	nfscl_cancelreqs(dsp);
}

/*
 * Cancel all RPCs for this "dsp" by closing the connection.
 * Also, mark the session as defunct.
 * If NFSCLDS_SAMECONN is set, the connection is shared with other DSs and
 * cannot be shut down.
 */
APPLESTATIC void
nfscl_cancelreqs(struct nfsclds *dsp)
{
	struct __rpc_client *cl;
	static int non_event;

	NFSLOCKDS(dsp);
	if ((dsp->nfsclds_flags & (NFSCLDS_CLOSED | NFSCLDS_SAMECONN)) == 0 &&
	    dsp->nfsclds_sockp != NULL &&
	    dsp->nfsclds_sockp->nr_client != NULL) {
		dsp->nfsclds_flags |= NFSCLDS_CLOSED;
		cl = dsp->nfsclds_sockp->nr_client;
		dsp->nfsclds_sess.nfsess_defunct = 1;
		NFSUNLOCKDS(dsp);
		CLNT_CLOSE(cl);
		/*
		 * This 1sec sleep is done to reduce the number of reconnect
		 * attempts made on the DS while it has failed.
		 */
		tsleep(&non_event, PVFS, "ndscls", hz);
		return;
	}
	NFSUNLOCKDS(dsp);
}

/*
 * Dereference a layout.
 */
void
nfscl_rellayout(struct nfscllayout *lyp, int exclocked)
{

	NFSLOCKCLSTATE();
	if (exclocked != 0)
		nfsv4_unlock(&lyp->nfsly_lock, 0);
	else
		nfsv4_relref(&lyp->nfsly_lock);
	NFSUNLOCKCLSTATE();
}

/*
 * Search for a devinfo by deviceid. If one is found, return it after
 * acquiring a reference count on it.
 */
struct nfscldevinfo *
nfscl_getdevinfo(struct nfsclclient *clp, uint8_t *deviceid,
    struct nfscldevinfo *dip)
{

	NFSLOCKCLSTATE();
	if (dip == NULL)
		dip = nfscl_finddevinfo(clp, deviceid);
	if (dip != NULL)
		dip->nfsdi_refcnt++;
	NFSUNLOCKCLSTATE();
	return (dip);
}

/*
 * Dereference a devinfo structure.
 */
static void
nfscl_reldevinfo_locked(struct nfscldevinfo *dip)
{

	dip->nfsdi_refcnt--;
	if (dip->nfsdi_refcnt == 0)
		wakeup(&dip->nfsdi_refcnt);
}

/*
 * Dereference a devinfo structure.
 */
void
nfscl_reldevinfo(struct nfscldevinfo *dip)
{

	NFSLOCKCLSTATE();
	nfscl_reldevinfo_locked(dip);
	NFSUNLOCKCLSTATE();
}

/*
 * Find a layout for this file handle. Return NULL upon failure.
 */
static struct nfscllayout *
nfscl_findlayout(struct nfsclclient *clp, u_int8_t *fhp, int fhlen)
{
	struct nfscllayout *lyp;

	LIST_FOREACH(lyp, NFSCLLAYOUTHASH(clp, fhp, fhlen), nfsly_hash)
		if (lyp->nfsly_fhlen == fhlen &&
		    !NFSBCMP(lyp->nfsly_fh, fhp, fhlen))
			break;
	return (lyp);
}

/*
 * Find a devinfo for this deviceid. Return NULL upon failure.
 */
static struct nfscldevinfo *
nfscl_finddevinfo(struct nfsclclient *clp, uint8_t *deviceid)
{
	struct nfscldevinfo *dip;

	LIST_FOREACH(dip, &clp->nfsc_devinfo, nfsdi_list)
		if (NFSBCMP(dip->nfsdi_deviceid, deviceid, NFSX_V4DEVICEID)
		    == 0)
			break;
	return (dip);
}

/*
 * Merge the new file layout list into the main one, maintaining it in
 * increasing offset order.
 */
static void
nfscl_mergeflayouts(struct nfsclflayouthead *fhlp,
    struct nfsclflayouthead *newfhlp)
{
	struct nfsclflayout *flp, *nflp, *prevflp, *tflp;

	flp = LIST_FIRST(fhlp);
	prevflp = NULL;
	LIST_FOREACH_SAFE(nflp, newfhlp, nfsfl_list, tflp) {
		while (flp != NULL && flp->nfsfl_off < nflp->nfsfl_off) {
			prevflp = flp;
			flp = LIST_NEXT(flp, nfsfl_list);
		}
		if (prevflp == NULL)
			LIST_INSERT_HEAD(fhlp, nflp, nfsfl_list);
		else
			LIST_INSERT_AFTER(prevflp, nflp, nfsfl_list);
		prevflp = nflp;
	}
}

/*
 * Add this nfscldevinfo to the client, if it doesn't already exist.
 * This function consumes the structure pointed at by dip, if not NULL.
 */
APPLESTATIC int
nfscl_adddevinfo(struct nfsmount *nmp, struct nfscldevinfo *dip, int ind,
    struct nfsclflayout *flp)
{
	struct nfsclclient *clp;
	struct nfscldevinfo *tdip;
	uint8_t *dev;

	NFSLOCKCLSTATE();
	clp = nmp->nm_clp;
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		if (dip != NULL)
			free(dip, M_NFSDEVINFO);
		return (ENODEV);
	}
	if ((flp->nfsfl_flags & NFSFL_FILE) != 0)
		dev = flp->nfsfl_dev;
	else
		dev = flp->nfsfl_ffm[ind].dev;
	tdip = nfscl_finddevinfo(clp, dev);
	if (tdip != NULL) {
		tdip->nfsdi_layoutrefs++;
		if ((flp->nfsfl_flags & NFSFL_FILE) != 0)
			flp->nfsfl_devp = tdip;
		else
			flp->nfsfl_ffm[ind].devp = tdip;
		nfscl_reldevinfo_locked(tdip);
		NFSUNLOCKCLSTATE();
		if (dip != NULL)
			free(dip, M_NFSDEVINFO);
		return (0);
	}
	if (dip != NULL) {
		LIST_INSERT_HEAD(&clp->nfsc_devinfo, dip, nfsdi_list);
		dip->nfsdi_layoutrefs = 1;
		if ((flp->nfsfl_flags & NFSFL_FILE) != 0)
			flp->nfsfl_devp = dip;
		else
			flp->nfsfl_ffm[ind].devp = dip;
	}
	NFSUNLOCKCLSTATE();
	if (dip == NULL)
		return (ENODEV);
	return (0);
}

/*
 * Free up a layout structure and associated file layout structure(s).
 */
APPLESTATIC void
nfscl_freelayout(struct nfscllayout *layp)
{
	struct nfsclflayout *flp, *nflp;
	struct nfsclrecalllayout *rp, *nrp;

	LIST_FOREACH_SAFE(flp, &layp->nfsly_flayread, nfsfl_list, nflp) {
		LIST_REMOVE(flp, nfsfl_list);
		nfscl_freeflayout(flp);
	}
	LIST_FOREACH_SAFE(flp, &layp->nfsly_flayrw, nfsfl_list, nflp) {
		LIST_REMOVE(flp, nfsfl_list);
		nfscl_freeflayout(flp);
	}
	LIST_FOREACH_SAFE(rp, &layp->nfsly_recall, nfsrecly_list, nrp) {
		LIST_REMOVE(rp, nfsrecly_list);
		free(rp, M_NFSLAYRECALL);
	}
	nfscl_layoutcnt--;
	free(layp, M_NFSLAYOUT);
}

/*
 * Free up a file layout structure.
 */
APPLESTATIC void
nfscl_freeflayout(struct nfsclflayout *flp)
{
	int i, j;

	if ((flp->nfsfl_flags & NFSFL_FILE) != 0) {
		for (i = 0; i < flp->nfsfl_fhcnt; i++)
			free(flp->nfsfl_fh[i], M_NFSFH);
		if (flp->nfsfl_devp != NULL)
			flp->nfsfl_devp->nfsdi_layoutrefs--;
	}
	if ((flp->nfsfl_flags & NFSFL_FLEXFILE) != 0)
		for (i = 0; i < flp->nfsfl_mirrorcnt; i++) {
			for (j = 0; j < flp->nfsfl_ffm[i].fhcnt; j++)
				free(flp->nfsfl_ffm[i].fh[j], M_NFSFH);
			if (flp->nfsfl_ffm[i].devp != NULL)	
				flp->nfsfl_ffm[i].devp->nfsdi_layoutrefs--;	
		}
	free(flp, M_NFSFLAYOUT);
}

/*
 * Free up a file layout devinfo structure.
 */
APPLESTATIC void
nfscl_freedevinfo(struct nfscldevinfo *dip)
{

	free(dip, M_NFSDEVINFO);
}

/*
 * Mark any layouts that match as recalled.
 */
static int
nfscl_layoutrecall(int recalltype, struct nfscllayout *lyp, uint32_t iomode,
    uint64_t off, uint64_t len, uint32_t stateseqid, uint32_t stat, uint32_t op,
    char *devid, struct nfsclrecalllayout *recallp)
{
	struct nfsclrecalllayout *rp, *orp;

	recallp->nfsrecly_recalltype = recalltype;
	recallp->nfsrecly_iomode = iomode;
	recallp->nfsrecly_stateseqid = stateseqid;
	recallp->nfsrecly_off = off;
	recallp->nfsrecly_len = len;
	recallp->nfsrecly_stat = stat;
	recallp->nfsrecly_op = op;
	if (devid != NULL)
		NFSBCOPY(devid, recallp->nfsrecly_devid, NFSX_V4DEVICEID);
	/*
	 * Order the list as file returns first, followed by fsid and any
	 * returns, both in increasing stateseqid order.
	 * Note that the seqids wrap around, so 1 is after 0xffffffff.
	 * (I'm not sure this is correct because I find RFC5661 confusing
	 *  on this, but hopefully it will work ok.)
	 */
	orp = NULL;
	LIST_FOREACH(rp, &lyp->nfsly_recall, nfsrecly_list) {
		orp = rp;
		if ((recalltype == NFSLAYOUTRETURN_FILE &&
		     (rp->nfsrecly_recalltype != NFSLAYOUTRETURN_FILE ||
		      nfscl_seq(stateseqid, rp->nfsrecly_stateseqid) != 0)) ||
		    (recalltype != NFSLAYOUTRETURN_FILE &&
		     rp->nfsrecly_recalltype != NFSLAYOUTRETURN_FILE &&
		     nfscl_seq(stateseqid, rp->nfsrecly_stateseqid) != 0)) {
			LIST_INSERT_BEFORE(rp, recallp, nfsrecly_list);
			break;
		}

		/*
		 * Put any error return on all the file returns that will
		 * preceed this one.
		 */
		if (rp->nfsrecly_recalltype == NFSLAYOUTRETURN_FILE &&
		   stat != 0 && rp->nfsrecly_stat == 0) {
			rp->nfsrecly_stat = stat;
			rp->nfsrecly_op = op;
			if (devid != NULL)
				NFSBCOPY(devid, rp->nfsrecly_devid,
				    NFSX_V4DEVICEID);
		}
	}
	if (rp == NULL) {
		if (orp == NULL)
			LIST_INSERT_HEAD(&lyp->nfsly_recall, recallp,
			    nfsrecly_list);
		else
			LIST_INSERT_AFTER(orp, recallp, nfsrecly_list);
	}
	lyp->nfsly_flags |= NFSLY_RECALL;
	wakeup(lyp->nfsly_clp);
	return (0);
}

/*
 * Compare the two seqids for ordering. The trick is that the seqids can
 * wrap around from 0xffffffff->0, so check for the cases where one
 * has wrapped around.
 * Return 1 if seqid1 comes before seqid2, 0 otherwise.
 */
static int
nfscl_seq(uint32_t seqid1, uint32_t seqid2)
{

	if (seqid2 > seqid1 && (seqid2 - seqid1) >= 0x7fffffff)
		/* seqid2 has wrapped around. */
		return (0);
	if (seqid1 > seqid2 && (seqid1 - seqid2) >= 0x7fffffff)
		/* seqid1 has wrapped around. */
		return (1);
	if (seqid1 <= seqid2)
		return (1);
	return (0);
}

/*
 * Do a layout return for each of the recalls.
 */
static void
nfscl_layoutreturn(struct nfsmount *nmp, struct nfscllayout *lyp,
    struct ucred *cred, NFSPROC_T *p)
{
	struct nfsclrecalllayout *rp;
	nfsv4stateid_t stateid;
	int layouttype;

	NFSBCOPY(lyp->nfsly_stateid.other, stateid.other, NFSX_STATEIDOTHER);
	stateid.seqid = lyp->nfsly_stateid.seqid;
	if ((lyp->nfsly_flags & NFSLY_FILES) != 0)
		layouttype = NFSLAYOUT_NFSV4_1_FILES;
	else
		layouttype = NFSLAYOUT_FLEXFILE;
	LIST_FOREACH(rp, &lyp->nfsly_recall, nfsrecly_list) {
		(void)nfsrpc_layoutreturn(nmp, lyp->nfsly_fh,
		    lyp->nfsly_fhlen, 0, layouttype,
		    rp->nfsrecly_iomode, rp->nfsrecly_recalltype,
		    rp->nfsrecly_off, rp->nfsrecly_len,
		    &stateid, cred, p, rp->nfsrecly_stat, rp->nfsrecly_op,
		    rp->nfsrecly_devid);
	}
}

/*
 * Do the layout commit for a file layout.
 */
static void
nfscl_dolayoutcommit(struct nfsmount *nmp, struct nfscllayout *lyp,
    struct ucred *cred, NFSPROC_T *p)
{
	struct nfsclflayout *flp;
	uint64_t len;
	int error, layouttype;

	if ((lyp->nfsly_flags & NFSLY_FILES) != 0)
		layouttype = NFSLAYOUT_NFSV4_1_FILES;
	else
		layouttype = NFSLAYOUT_FLEXFILE;
	LIST_FOREACH(flp, &lyp->nfsly_flayrw, nfsfl_list) {
		if (layouttype == NFSLAYOUT_FLEXFILE &&
		    (flp->nfsfl_fflags & NFSFLEXFLAG_NO_LAYOUTCOMMIT) != 0) {
			NFSCL_DEBUG(4, "Flex file: no layoutcommit\n");
			/* If not supported, don't bother doing it. */
			NFSLOCKMNT(nmp);
			nmp->nm_state |= NFSSTA_NOLAYOUTCOMMIT;
			NFSUNLOCKMNT(nmp);
			break;
		} else if (flp->nfsfl_off <= lyp->nfsly_lastbyte) {
			len = flp->nfsfl_end - flp->nfsfl_off;
			error = nfsrpc_layoutcommit(nmp, lyp->nfsly_fh,
			    lyp->nfsly_fhlen, 0, flp->nfsfl_off, len,
			    lyp->nfsly_lastbyte, &lyp->nfsly_stateid,
			    layouttype, cred, p, NULL);
			NFSCL_DEBUG(4, "layoutcommit err=%d\n", error);
			if (error == NFSERR_NOTSUPP) {
				/* If not supported, don't bother doing it. */
				NFSLOCKMNT(nmp);
				nmp->nm_state |= NFSSTA_NOLAYOUTCOMMIT;
				NFSUNLOCKMNT(nmp);
				break;
			}
		}
	}
}

/*
 * Commit all layouts for a file (vnode).
 */
int
nfscl_layoutcommit(vnode_t vp, NFSPROC_T *p)
{
	struct nfsclclient *clp;
	struct nfscllayout *lyp;
	struct nfsnode *np = VTONFS(vp);
	mount_t mp;
	struct nfsmount *nmp;

	mp = vnode_mount(vp);
	nmp = VFSTONFS(mp);
	if (NFSHASNOLAYOUTCOMMIT(nmp))
		return (0);
	NFSLOCKCLSTATE();
	clp = nmp->nm_clp;
	if (clp == NULL) {
		NFSUNLOCKCLSTATE();
		return (EPERM);
	}
	lyp = nfscl_findlayout(clp, np->n_fhp->nfh_fh, np->n_fhp->nfh_len);
	if (lyp == NULL) {
		NFSUNLOCKCLSTATE();
		return (EPERM);
	}
	nfsv4_getref(&lyp->nfsly_lock, NULL, NFSCLSTATEMUTEXPTR, mp);
	if (NFSCL_FORCEDISM(mp)) {
		NFSUNLOCKCLSTATE();
		return (EPERM);
	}
tryagain:
	if ((lyp->nfsly_flags & NFSLY_WRITTEN) != 0) {
		lyp->nfsly_flags &= ~NFSLY_WRITTEN;
		NFSUNLOCKCLSTATE();
		NFSCL_DEBUG(4, "do layoutcommit2\n");
		nfscl_dolayoutcommit(clp->nfsc_nmp, lyp, NFSPROCCRED(p), p);
		NFSLOCKCLSTATE();
		goto tryagain;
	}
	nfsv4_relref(&lyp->nfsly_lock);
	NFSUNLOCKCLSTATE();
	return (0);
}

