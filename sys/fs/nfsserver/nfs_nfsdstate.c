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

#ifndef APPLEKEXT
#include <sys/extattr.h>
#include <fs/nfs/nfsport.h>

struct nfsrv_stablefirst nfsrv_stablefirst;
int nfsrv_issuedelegs = 0;
int nfsrv_dolocallocks = 0;
struct nfsv4lock nfsv4rootfs_lock;
time_t nfsdev_time = 0;
int nfsrv_layouthashsize;
volatile int nfsrv_layoutcnt = 0;

extern int newnfs_numnfsd;
extern struct nfsstatsv1 nfsstatsv1;
extern int nfsrv_lease;
extern struct timeval nfsboottime;
extern u_int32_t newnfs_true, newnfs_false;
extern struct mtx nfsrv_dslock_mtx;
extern struct mtx nfsrv_recalllock_mtx;
extern struct mtx nfsrv_dontlistlock_mtx;
extern int nfsd_debuglevel;
extern u_int nfsrv_dsdirsize;
extern struct nfsdevicehead nfsrv_devidhead;
extern int nfsrv_doflexfile;
extern int nfsrv_maxpnfsmirror;
NFSV4ROOTLOCKMUTEX;
NFSSTATESPINLOCK;
extern struct nfsdontlisthead nfsrv_dontlisthead;
extern volatile int nfsrv_devidcnt;
extern struct nfslayouthead nfsrv_recalllisthead;
extern char *nfsrv_zeropnfsdat;

SYSCTL_DECL(_vfs_nfsd);
int	nfsrv_statehashsize = NFSSTATEHASHSIZE;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, statehashsize, CTLFLAG_RDTUN,
    &nfsrv_statehashsize, 0,
    "Size of state hash table set via loader.conf");

int	nfsrv_clienthashsize = NFSCLIENTHASHSIZE;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, clienthashsize, CTLFLAG_RDTUN,
    &nfsrv_clienthashsize, 0,
    "Size of client hash table set via loader.conf");

int	nfsrv_lockhashsize = NFSLOCKHASHSIZE;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, fhhashsize, CTLFLAG_RDTUN,
    &nfsrv_lockhashsize, 0,
    "Size of file handle hash table set via loader.conf");

int	nfsrv_sessionhashsize = NFSSESSIONHASHSIZE;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, sessionhashsize, CTLFLAG_RDTUN,
    &nfsrv_sessionhashsize, 0,
    "Size of session hash table set via loader.conf");

int	nfsrv_layouthighwater = NFSLAYOUTHIGHWATER;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, layouthighwater, CTLFLAG_RDTUN,
    &nfsrv_layouthighwater, 0,
    "High water mark for number of layouts set via loader.conf");

static int	nfsrv_v4statelimit = NFSRV_V4STATELIMIT;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, v4statelimit, CTLFLAG_RWTUN,
    &nfsrv_v4statelimit, 0,
    "High water limit for NFSv4 opens+locks+delegations");

static int	nfsrv_writedelegifpos = 0;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, writedelegifpos, CTLFLAG_RW,
    &nfsrv_writedelegifpos, 0,
    "Issue a write delegation for read opens if possible");

static int	nfsrv_allowreadforwriteopen = 1;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, allowreadforwriteopen, CTLFLAG_RW,
    &nfsrv_allowreadforwriteopen, 0,
    "Allow Reads to be done with Write Access StateIDs");

int	nfsrv_pnfsatime = 0;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, pnfsstrictatime, CTLFLAG_RW,
    &nfsrv_pnfsatime, 0,
    "For pNFS service, do Getattr ops to keep atime up-to-date");

int	nfsrv_flexlinuxhack = 0;
SYSCTL_INT(_vfs_nfsd, OID_AUTO, flexlinuxhack, CTLFLAG_RW,
    &nfsrv_flexlinuxhack, 0,
    "For Linux clients, hack around Flex File Layout bug");

/*
 * Hash lists for nfs V4.
 */
struct nfsclienthashhead	*nfsclienthash;
struct nfslockhashhead		*nfslockhash;
struct nfssessionhash		*nfssessionhash;
struct nfslayouthash		*nfslayouthash;
volatile int nfsrv_dontlistlen = 0;
#endif	/* !APPLEKEXT */

static u_int32_t nfsrv_openpluslock = 0, nfsrv_delegatecnt = 0;
static time_t nfsrvboottime;
static int nfsrv_returnoldstateid = 0, nfsrv_clients = 0;
static int nfsrv_clienthighwater = NFSRV_CLIENTHIGHWATER;
static int nfsrv_nogsscallback = 0;
static volatile int nfsrv_writedelegcnt = 0;
static int nfsrv_faildscnt;

/* local functions */
static void nfsrv_dumpaclient(struct nfsclient *clp,
    struct nfsd_dumpclients *dumpp);
static void nfsrv_freeopenowner(struct nfsstate *stp, int cansleep,
    NFSPROC_T *p);
static int nfsrv_freeopen(struct nfsstate *stp, vnode_t vp, int cansleep,
    NFSPROC_T *p);
static void nfsrv_freelockowner(struct nfsstate *stp, vnode_t vp, int cansleep,
    NFSPROC_T *p);
static void nfsrv_freeallnfslocks(struct nfsstate *stp, vnode_t vp,
    int cansleep, NFSPROC_T *p);
static void nfsrv_freenfslock(struct nfslock *lop);
static void nfsrv_freenfslockfile(struct nfslockfile *lfp);
static void nfsrv_freedeleg(struct nfsstate *);
static int nfsrv_getstate(struct nfsclient *clp, nfsv4stateid_t *stateidp, 
    u_int32_t flags, struct nfsstate **stpp);
static void nfsrv_getowner(struct nfsstatehead *hp, struct nfsstate *new_stp,
    struct nfsstate **stpp);
static int nfsrv_getlockfh(vnode_t vp, u_short flags,
    struct nfslockfile *new_lfp, fhandle_t *nfhp, NFSPROC_T *p);
static int nfsrv_getlockfile(u_short flags, struct nfslockfile **new_lfpp,
    struct nfslockfile **lfpp, fhandle_t *nfhp, int lockit);
static void nfsrv_insertlock(struct nfslock *new_lop,
    struct nfslock *insert_lop, struct nfsstate *stp, struct nfslockfile *lfp);
static void nfsrv_updatelock(struct nfsstate *stp, struct nfslock **new_lopp,
    struct nfslock **other_lopp, struct nfslockfile *lfp);
static int nfsrv_getipnumber(u_char *cp);
static int nfsrv_checkrestart(nfsquad_t clientid, u_int32_t flags,
    nfsv4stateid_t *stateidp, int specialid);
static int nfsrv_checkgrace(struct nfsrv_descript *nd, struct nfsclient *clp,
    u_int32_t flags);
static int nfsrv_docallback(struct nfsclient *clp, int procnum,
    nfsv4stateid_t *stateidp, int trunc, fhandle_t *fhp,
    struct nfsvattr *nap, nfsattrbit_t *attrbitp, int laytype, NFSPROC_T *p);
static int nfsrv_cbcallargs(struct nfsrv_descript *nd, struct nfsclient *clp,
    uint32_t callback, int op, const char *optag, struct nfsdsession **sepp);
static u_int32_t nfsrv_nextclientindex(void);
static u_int32_t nfsrv_nextstateindex(struct nfsclient *clp);
static void nfsrv_markstable(struct nfsclient *clp);
static void nfsrv_markreclaim(struct nfsclient *clp);
static int nfsrv_checkstable(struct nfsclient *clp);
static int nfsrv_clientconflict(struct nfsclient *clp, int *haslockp, struct 
    vnode *vp, NFSPROC_T *p);
static int nfsrv_delegconflict(struct nfsstate *stp, int *haslockp,
    NFSPROC_T *p, vnode_t vp);
static int nfsrv_cleandeleg(vnode_t vp, struct nfslockfile *lfp,
    struct nfsclient *clp, int *haslockp, NFSPROC_T *p);
static int nfsrv_notsamecredname(struct nfsrv_descript *nd,
    struct nfsclient *clp);
static time_t nfsrv_leaseexpiry(void);
static void nfsrv_delaydelegtimeout(struct nfsstate *stp);
static int nfsrv_checkseqid(struct nfsrv_descript *nd, u_int32_t seqid,
    struct nfsstate *stp, struct nfsrvcache *op);
static int nfsrv_nootherstate(struct nfsstate *stp);
static int nfsrv_locallock(vnode_t vp, struct nfslockfile *lfp, int flags,
    uint64_t first, uint64_t end, struct nfslockconflict *cfp, NFSPROC_T *p);
static void nfsrv_localunlock(vnode_t vp, struct nfslockfile *lfp,
    uint64_t init_first, uint64_t init_end, NFSPROC_T *p);
static int nfsrv_dolocal(vnode_t vp, struct nfslockfile *lfp, int flags,
    int oldflags, uint64_t first, uint64_t end, struct nfslockconflict *cfp,
    NFSPROC_T *p);
static void nfsrv_locallock_rollback(vnode_t vp, struct nfslockfile *lfp,
    NFSPROC_T *p);
static void nfsrv_locallock_commit(struct nfslockfile *lfp, int flags,
    uint64_t first, uint64_t end);
static void nfsrv_locklf(struct nfslockfile *lfp);
static void nfsrv_unlocklf(struct nfslockfile *lfp);
static struct nfsdsession *nfsrv_findsession(uint8_t *sessionid);
static int nfsrv_freesession(struct nfsdsession *sep, uint8_t *sessionid);
static int nfsv4_setcbsequence(struct nfsrv_descript *nd, struct nfsclient *clp,
    int dont_replycache, struct nfsdsession **sepp);
static int nfsv4_getcbsession(struct nfsclient *clp, struct nfsdsession **sepp);
static int nfsrv_addlayout(struct nfsrv_descript *nd, struct nfslayout **lypp,
    nfsv4stateid_t *stateidp, char *layp, int *layoutlenp, NFSPROC_T *p);
static void nfsrv_freelayout(struct nfslayouthead *lhp, struct nfslayout *lyp);
static void nfsrv_freelayoutlist(nfsquad_t clientid);
static void nfsrv_freelayouts(nfsquad_t *clid, fsid_t *fs, int laytype,
    int iomode);
static void nfsrv_freealllayouts(void);
static void nfsrv_freedevid(struct nfsdevice *ds);
static int nfsrv_setdsserver(char *dspathp, char *mdspathp, NFSPROC_T *p,
    struct nfsdevice **dsp);
static int nfsrv_delds(char *devid, NFSPROC_T *p);
static void nfsrv_deleteds(struct nfsdevice *fndds);
static void nfsrv_allocdevid(struct nfsdevice *ds, char *addr, char *dnshost);
static void nfsrv_freealldevids(void);
static void nfsrv_flexlayouterr(struct nfsrv_descript *nd, uint32_t *layp,
    int maxcnt, NFSPROC_T *p);
static int nfsrv_recalllayout(nfsquad_t clid, nfsv4stateid_t *stateidp,
    fhandle_t *fhp, struct nfslayout *lyp, int changed, int laytype,
    NFSPROC_T *p);
static int nfsrv_findlayout(nfsquad_t *clientidp, fhandle_t *fhp, int laytype,
    NFSPROC_T *, struct nfslayout **lypp);
static int nfsrv_fndclid(nfsquad_t *clidvec, nfsquad_t clid, int clidcnt);
static struct nfslayout *nfsrv_filelayout(struct nfsrv_descript *nd, int iomode,
    fhandle_t *fhp, fhandle_t *dsfhp, char *devid, fsid_t fs);
static struct nfslayout *nfsrv_flexlayout(struct nfsrv_descript *nd, int iomode,
    int mirrorcnt, fhandle_t *fhp, fhandle_t *dsfhp, char *devid, fsid_t fs);
static int nfsrv_dontlayout(fhandle_t *fhp);
static int nfsrv_createdsfile(vnode_t vp, fhandle_t *fhp, struct pnfsdsfile *pf,
    vnode_t dvp, struct nfsdevice *ds, struct ucred *cred, NFSPROC_T *p,
    vnode_t *tvpp);
static struct nfsdevice *nfsrv_findmirroredds(struct nfsmount *nmp);

/*
 * Scan the client list for a match and either return the current one,
 * create a new entry or return an error.
 * If returning a non-error, the clp structure must either be linked into
 * the client list or free'd.
 */
APPLESTATIC int
nfsrv_setclient(struct nfsrv_descript *nd, struct nfsclient **new_clpp,
    nfsquad_t *clientidp, nfsquad_t *confirmp, NFSPROC_T *p)
{
	struct nfsclient *clp = NULL, *new_clp = *new_clpp;
	int i, error = 0, ret;
	struct nfsstate *stp, *tstp;
	struct sockaddr_in *sad, *rad;
	struct nfsdsession *sep, *nsep;
	int zapit = 0, gotit, hasstate = 0, igotlock;
	static u_int64_t confirm_index = 0;

	/*
	 * Check for state resource limit exceeded.
	 */
	if (nfsrv_openpluslock > nfsrv_v4statelimit) {
		error = NFSERR_RESOURCE;
		goto out;
	}

	if (nfsrv_issuedelegs == 0 ||
	    ((nd->nd_flag & ND_GSS) != 0 && nfsrv_nogsscallback != 0))
		/*
		 * Don't do callbacks when delegations are disabled or
		 * for AUTH_GSS unless enabled via nfsrv_nogsscallback.
		 * If establishing a callback connection is attempted
		 * when a firewall is blocking the callback path, the
		 * server may wait too long for the connect attempt to
		 * succeed during the Open. Some clients, such as Linux,
		 * may timeout and give up on the Open before the server
		 * replies. Also, since AUTH_GSS callbacks are not
		 * yet interoperability tested, they might cause the
		 * server to crap out, if they get past the Init call to
		 * the client.
		 */
		new_clp->lc_program = 0;

	/* Lock out other nfsd threads */
	NFSLOCKV4ROOTMUTEX();
	nfsv4_relref(&nfsv4rootfs_lock);
	do {
		igotlock = nfsv4_lock(&nfsv4rootfs_lock, 1, NULL,
		    NFSV4ROOTLOCKMUTEXPTR, NULL);
	} while (!igotlock);
	NFSUNLOCKV4ROOTMUTEX();

	/*
	 * Search for a match in the client list.
	 */
	gotit = i = 0;
	while (i < nfsrv_clienthashsize && !gotit) {
	    LIST_FOREACH(clp, &nfsclienthash[i], lc_hash) {
		if (new_clp->lc_idlen == clp->lc_idlen &&
		    !NFSBCMP(new_clp->lc_id, clp->lc_id, clp->lc_idlen)) {
			gotit = 1;
			break;
		}
	    }
	    if (gotit == 0)
		i++;
	}
	if (!gotit ||
	    (clp->lc_flags & (LCL_NEEDSCONFIRM | LCL_ADMINREVOKED))) {
		if ((nd->nd_flag & ND_NFSV41) != 0 && confirmp->lval[1] != 0) {
			/*
			 * For NFSv4.1, if confirmp->lval[1] is non-zero, the
			 * client is trying to update a confirmed clientid.
			 */
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
			confirmp->lval[1] = 0;
			error = NFSERR_NOENT;
			goto out;
		}
		/*
		 * Get rid of the old one.
		 */
		if (i != nfsrv_clienthashsize) {
			LIST_REMOVE(clp, lc_hash);
			nfsrv_cleanclient(clp, p);
			nfsrv_freedeleglist(&clp->lc_deleg);
			nfsrv_freedeleglist(&clp->lc_olddeleg);
			zapit = 1;
		}
		/*
		 * Add it after assigning a client id to it.
		 */
		new_clp->lc_flags |= LCL_NEEDSCONFIRM;
		if ((nd->nd_flag & ND_NFSV41) != 0)
			new_clp->lc_confirm.lval[0] = confirmp->lval[0] =
			    ++confirm_index;
		else
			confirmp->qval = new_clp->lc_confirm.qval =
			    ++confirm_index;
		clientidp->lval[0] = new_clp->lc_clientid.lval[0] =
		    (u_int32_t)nfsrvboottime;
		clientidp->lval[1] = new_clp->lc_clientid.lval[1] =
		    nfsrv_nextclientindex();
		new_clp->lc_stateindex = 0;
		new_clp->lc_statemaxindex = 0;
		new_clp->lc_cbref = 0;
		new_clp->lc_expiry = nfsrv_leaseexpiry();
		LIST_INIT(&new_clp->lc_open);
		LIST_INIT(&new_clp->lc_deleg);
		LIST_INIT(&new_clp->lc_olddeleg);
		LIST_INIT(&new_clp->lc_session);
		for (i = 0; i < nfsrv_statehashsize; i++)
			LIST_INIT(&new_clp->lc_stateid[i]);
		LIST_INSERT_HEAD(NFSCLIENTHASH(new_clp->lc_clientid), new_clp,
		    lc_hash);
		nfsstatsv1.srvclients++;
		nfsrv_openpluslock++;
		nfsrv_clients++;
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();
		if (zapit)
			nfsrv_zapclient(clp, p);
		*new_clpp = NULL;
		goto out;
	}

	/*
	 * Now, handle the cases where the id is already issued.
	 */
	if (nfsrv_notsamecredname(nd, clp)) {
	    /*
	     * Check to see if there is expired state that should go away.
	     */
	    if (clp->lc_expiry < NFSD_MONOSEC &&
	        (!LIST_EMPTY(&clp->lc_open) || !LIST_EMPTY(&clp->lc_deleg))) {
		nfsrv_cleanclient(clp, p);
		nfsrv_freedeleglist(&clp->lc_deleg);
	    }

	    /*
	     * If there is outstanding state, then reply NFSERR_CLIDINUSE per
	     * RFC3530 Sec. 8.1.2 last para.
	     */
	    if (!LIST_EMPTY(&clp->lc_deleg)) {
		hasstate = 1;
	    } else if (LIST_EMPTY(&clp->lc_open)) {
		hasstate = 0;
	    } else {
		hasstate = 0;
		/* Look for an Open on the OpenOwner */
		LIST_FOREACH(stp, &clp->lc_open, ls_list) {
		    if (!LIST_EMPTY(&stp->ls_open)) {
			hasstate = 1;
			break;
		    }
		}
	    }
	    if (hasstate) {
		/*
		 * If the uid doesn't match, return NFSERR_CLIDINUSE after
		 * filling out the correct ipaddr and portnum.
		 */
		sad = NFSSOCKADDR(new_clp->lc_req.nr_nam, struct sockaddr_in *);
		rad = NFSSOCKADDR(clp->lc_req.nr_nam, struct sockaddr_in *);
		sad->sin_addr.s_addr = rad->sin_addr.s_addr;
		sad->sin_port = rad->sin_port;
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();
		error = NFSERR_CLIDINUSE;
		goto out;
	    }
	}

	if (NFSBCMP(new_clp->lc_verf, clp->lc_verf, NFSX_VERF)) {
		/*
		 * If the verifier has changed, the client has rebooted
		 * and a new client id is issued. The old state info
		 * can be thrown away once the SETCLIENTID_CONFIRM occurs.
		 */
		LIST_REMOVE(clp, lc_hash);

		/* Get rid of all sessions on this clientid. */
		LIST_FOREACH_SAFE(sep, &clp->lc_session, sess_list, nsep) {
			ret = nfsrv_freesession(sep, NULL);
			if (ret != 0)
				printf("nfsrv_setclient: verifier changed free"
				    " session failed=%d\n", ret);
		}

		new_clp->lc_flags |= LCL_NEEDSCONFIRM;
		if ((nd->nd_flag & ND_NFSV41) != 0)
			new_clp->lc_confirm.lval[0] = confirmp->lval[0] =
			    ++confirm_index;
		else
			confirmp->qval = new_clp->lc_confirm.qval =
			    ++confirm_index;
		clientidp->lval[0] = new_clp->lc_clientid.lval[0] =
		    nfsrvboottime;
		clientidp->lval[1] = new_clp->lc_clientid.lval[1] =
		    nfsrv_nextclientindex();
		new_clp->lc_stateindex = 0;
		new_clp->lc_statemaxindex = 0;
		new_clp->lc_cbref = 0;
		new_clp->lc_expiry = nfsrv_leaseexpiry();

		/*
		 * Save the state until confirmed.
		 */
		LIST_NEWHEAD(&new_clp->lc_open, &clp->lc_open, ls_list);
		LIST_FOREACH(tstp, &new_clp->lc_open, ls_list)
			tstp->ls_clp = new_clp;
		LIST_NEWHEAD(&new_clp->lc_deleg, &clp->lc_deleg, ls_list);
		LIST_FOREACH(tstp, &new_clp->lc_deleg, ls_list)
			tstp->ls_clp = new_clp;
		LIST_NEWHEAD(&new_clp->lc_olddeleg, &clp->lc_olddeleg,
		    ls_list);
		LIST_FOREACH(tstp, &new_clp->lc_olddeleg, ls_list)
			tstp->ls_clp = new_clp;
		for (i = 0; i < nfsrv_statehashsize; i++) {
			LIST_NEWHEAD(&new_clp->lc_stateid[i],
			    &clp->lc_stateid[i], ls_hash);
			LIST_FOREACH(tstp, &new_clp->lc_stateid[i], ls_hash)
				tstp->ls_clp = new_clp;
		}
		LIST_INIT(&new_clp->lc_session);
		LIST_INSERT_HEAD(NFSCLIENTHASH(new_clp->lc_clientid), new_clp,
		    lc_hash);
		nfsstatsv1.srvclients++;
		nfsrv_openpluslock++;
		nfsrv_clients++;
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();

		/*
		 * Must wait until any outstanding callback on the old clp
		 * completes.
		 */
		NFSLOCKSTATE();
		while (clp->lc_cbref) {
			clp->lc_flags |= LCL_WAKEUPWANTED;
			(void)mtx_sleep(clp, NFSSTATEMUTEXPTR, PZERO - 1,
			    "nfsd clp", 10 * hz);
		}
		NFSUNLOCKSTATE();
		nfsrv_zapclient(clp, p);
		*new_clpp = NULL;
		goto out;
	}

	/* For NFSv4.1, mark that we found a confirmed clientid. */
	if ((nd->nd_flag & ND_NFSV41) != 0) {
		clientidp->lval[0] = clp->lc_clientid.lval[0];
		clientidp->lval[1] = clp->lc_clientid.lval[1];
		confirmp->lval[0] = 0;	/* Ignored by client */
		confirmp->lval[1] = 1;
	} else {
		/*
		 * id and verifier match, so update the net address info
		 * and get rid of any existing callback authentication
		 * handle, so a new one will be acquired.
		 */
		LIST_REMOVE(clp, lc_hash);
		new_clp->lc_flags |= (LCL_NEEDSCONFIRM | LCL_DONTCLEAN);
		new_clp->lc_expiry = nfsrv_leaseexpiry();
		confirmp->qval = new_clp->lc_confirm.qval = ++confirm_index;
		clientidp->lval[0] = new_clp->lc_clientid.lval[0] =
		    clp->lc_clientid.lval[0];
		clientidp->lval[1] = new_clp->lc_clientid.lval[1] =
		    clp->lc_clientid.lval[1];
		new_clp->lc_delegtime = clp->lc_delegtime;
		new_clp->lc_stateindex = clp->lc_stateindex;
		new_clp->lc_statemaxindex = clp->lc_statemaxindex;
		new_clp->lc_cbref = 0;
		LIST_NEWHEAD(&new_clp->lc_open, &clp->lc_open, ls_list);
		LIST_FOREACH(tstp, &new_clp->lc_open, ls_list)
			tstp->ls_clp = new_clp;
		LIST_NEWHEAD(&new_clp->lc_deleg, &clp->lc_deleg, ls_list);
		LIST_FOREACH(tstp, &new_clp->lc_deleg, ls_list)
			tstp->ls_clp = new_clp;
		LIST_NEWHEAD(&new_clp->lc_olddeleg, &clp->lc_olddeleg, ls_list);
		LIST_FOREACH(tstp, &new_clp->lc_olddeleg, ls_list)
			tstp->ls_clp = new_clp;
		for (i = 0; i < nfsrv_statehashsize; i++) {
			LIST_NEWHEAD(&new_clp->lc_stateid[i],
			    &clp->lc_stateid[i], ls_hash);
			LIST_FOREACH(tstp, &new_clp->lc_stateid[i], ls_hash)
				tstp->ls_clp = new_clp;
		}
		LIST_INIT(&new_clp->lc_session);
		LIST_INSERT_HEAD(NFSCLIENTHASH(new_clp->lc_clientid), new_clp,
		    lc_hash);
		nfsstatsv1.srvclients++;
		nfsrv_openpluslock++;
		nfsrv_clients++;
	}
	NFSLOCKV4ROOTMUTEX();
	nfsv4_unlock(&nfsv4rootfs_lock, 1);
	NFSUNLOCKV4ROOTMUTEX();

	if ((nd->nd_flag & ND_NFSV41) == 0) {
		/*
		 * Must wait until any outstanding callback on the old clp
		 * completes.
		 */
		NFSLOCKSTATE();
		while (clp->lc_cbref) {
			clp->lc_flags |= LCL_WAKEUPWANTED;
			(void)mtx_sleep(clp, NFSSTATEMUTEXPTR, PZERO - 1,
			    "nfsdclp", 10 * hz);
		}
		NFSUNLOCKSTATE();
		nfsrv_zapclient(clp, p);
		*new_clpp = NULL;
	}

out:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Check to see if the client id exists and optionally confirm it.
 */
APPLESTATIC int
nfsrv_getclient(nfsquad_t clientid, int opflags, struct nfsclient **clpp,
    struct nfsdsession *nsep, nfsquad_t confirm, uint32_t cbprogram,
    struct nfsrv_descript *nd, NFSPROC_T *p)
{
	struct nfsclient *clp;
	struct nfsstate *stp;
	int i;
	struct nfsclienthashhead *hp;
	int error = 0, igotlock, doneok;
	struct nfssessionhash *shp;
	struct nfsdsession *sep;
	uint64_t sessid[2];
	static uint64_t next_sess = 0;

	if (clpp)
		*clpp = NULL;
	if ((nd == NULL || (nd->nd_flag & ND_NFSV41) == 0 ||
	    opflags != CLOPS_RENEW) && nfsrvboottime != clientid.lval[0]) {
		error = NFSERR_STALECLIENTID;
		goto out;
	}

	/*
	 * If called with opflags == CLOPS_RENEW, the State Lock is
	 * already held. Otherwise, we need to get either that or,
	 * for the case of Confirm, lock out the nfsd threads.
	 */
	if (opflags & CLOPS_CONFIRM) {
		NFSLOCKV4ROOTMUTEX();
		nfsv4_relref(&nfsv4rootfs_lock);
		do {
			igotlock = nfsv4_lock(&nfsv4rootfs_lock, 1, NULL,
			    NFSV4ROOTLOCKMUTEXPTR, NULL);
		} while (!igotlock);
		/*
		 * Create a new sessionid here, since we need to do it where
		 * there is a mutex held to serialize update of next_sess.
		 */
		if ((nd->nd_flag & ND_NFSV41) != 0) {
			sessid[0] = ++next_sess;
			sessid[1] = clientid.qval;
		}
		NFSUNLOCKV4ROOTMUTEX();
	} else if (opflags != CLOPS_RENEW) {
		NFSLOCKSTATE();
	}

	/* For NFSv4.1, the clp is acquired from the associated session. */
	if (nd != NULL && (nd->nd_flag & ND_NFSV41) != 0 &&
	    opflags == CLOPS_RENEW) {
		clp = NULL;
		if ((nd->nd_flag & ND_HASSEQUENCE) != 0) {
			shp = NFSSESSIONHASH(nd->nd_sessionid);
			NFSLOCKSESSION(shp);
			sep = nfsrv_findsession(nd->nd_sessionid);
			if (sep != NULL)
				clp = sep->sess_clp;
			NFSUNLOCKSESSION(shp);
		}
	} else {
		hp = NFSCLIENTHASH(clientid);
		LIST_FOREACH(clp, hp, lc_hash) {
			if (clp->lc_clientid.lval[1] == clientid.lval[1])
				break;
		}
	}
	if (clp == NULL) {
		if (opflags & CLOPS_CONFIRM)
			error = NFSERR_STALECLIENTID;
		else
			error = NFSERR_EXPIRED;
	} else if (clp->lc_flags & LCL_ADMINREVOKED) {
		/*
		 * If marked admin revoked, just return the error.
		 */
		error = NFSERR_ADMINREVOKED;
	}
	if (error) {
		if (opflags & CLOPS_CONFIRM) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		} else if (opflags != CLOPS_RENEW) {
			NFSUNLOCKSTATE();
		}
		goto out;
	}

	/*
	 * Perform any operations specified by the opflags.
	 */
	if (opflags & CLOPS_CONFIRM) {
		if (((nd->nd_flag & ND_NFSV41) != 0 &&
		     clp->lc_confirm.lval[0] != confirm.lval[0]) ||
		    ((nd->nd_flag & ND_NFSV41) == 0 &&
		     clp->lc_confirm.qval != confirm.qval))
			error = NFSERR_STALECLIENTID;
		else if (nfsrv_notsamecredname(nd, clp))
			error = NFSERR_CLIDINUSE;

		if (!error) {
		    if ((clp->lc_flags & (LCL_NEEDSCONFIRM | LCL_DONTCLEAN)) ==
			LCL_NEEDSCONFIRM) {
			/*
			 * Hang onto the delegations (as old delegations)
			 * for an Open with CLAIM_DELEGATE_PREV unless in
			 * grace, but get rid of the rest of the state.
			 */
			nfsrv_cleanclient(clp, p);
			nfsrv_freedeleglist(&clp->lc_olddeleg);
			if (nfsrv_checkgrace(nd, clp, 0)) {
			    /* In grace, so just delete delegations */
			    nfsrv_freedeleglist(&clp->lc_deleg);
			} else {
			    LIST_FOREACH(stp, &clp->lc_deleg, ls_list)
				stp->ls_flags |= NFSLCK_OLDDELEG;
			    clp->lc_delegtime = NFSD_MONOSEC +
				nfsrv_lease + NFSRV_LEASEDELTA;
			    LIST_NEWHEAD(&clp->lc_olddeleg, &clp->lc_deleg,
				ls_list);
			}
			if ((nd->nd_flag & ND_NFSV41) != 0)
			    clp->lc_program = cbprogram;
		    }
		    clp->lc_flags &= ~(LCL_NEEDSCONFIRM | LCL_DONTCLEAN);
		    if (clp->lc_program)
			clp->lc_flags |= LCL_NEEDSCBNULL;
		    /* For NFSv4.1, link the session onto the client. */
		    if (nsep != NULL) {
			/* Hold a reference on the xprt for a backchannel. */
			if ((nsep->sess_crflags & NFSV4CRSESS_CONNBACKCHAN)
			    != 0) {
			    if (clp->lc_req.nr_client == NULL)
				clp->lc_req.nr_client = (struct __rpc_client *)
				    clnt_bck_create(nd->nd_xprt->xp_socket,
				    cbprogram, NFSV4_CBVERS);
			    if (clp->lc_req.nr_client != NULL) {
				SVC_ACQUIRE(nd->nd_xprt);
				nd->nd_xprt->xp_p2 =
				    clp->lc_req.nr_client->cl_private;
				/* Disable idle timeout. */
				nd->nd_xprt->xp_idletimeout = 0;
				nsep->sess_cbsess.nfsess_xprt = nd->nd_xprt;
			    } else
				nsep->sess_crflags &= ~NFSV4CRSESS_CONNBACKCHAN;
			}
			NFSBCOPY(sessid, nsep->sess_sessionid,
			    NFSX_V4SESSIONID);
			NFSBCOPY(sessid, nsep->sess_cbsess.nfsess_sessionid,
			    NFSX_V4SESSIONID);
			shp = NFSSESSIONHASH(nsep->sess_sessionid);
			NFSLOCKSTATE();
			NFSLOCKSESSION(shp);
			LIST_INSERT_HEAD(&shp->list, nsep, sess_hash);
			LIST_INSERT_HEAD(&clp->lc_session, nsep, sess_list);
			nsep->sess_clp = clp;
			NFSUNLOCKSESSION(shp);
			NFSUNLOCKSTATE();
		    }
		}
	} else if (clp->lc_flags & LCL_NEEDSCONFIRM) {
		error = NFSERR_EXPIRED;
	}

	/*
	 * If called by the Renew Op, we must check the principal.
	 */
	if (!error && (opflags & CLOPS_RENEWOP)) {
	    if (nfsrv_notsamecredname(nd, clp)) {
		doneok = 0;
		for (i = 0; i < nfsrv_statehashsize && doneok == 0; i++) {
		    LIST_FOREACH(stp, &clp->lc_stateid[i], ls_hash) {
			if ((stp->ls_flags & NFSLCK_OPEN) &&
			    stp->ls_uid == nd->nd_cred->cr_uid) {
				doneok = 1;
				break;
			}
		    }
		}
		if (!doneok)
			error = NFSERR_ACCES;
	    }
	    if (!error && (clp->lc_flags & LCL_CBDOWN))
		error = NFSERR_CBPATHDOWN;
	}
	if ((!error || error == NFSERR_CBPATHDOWN) &&
	     (opflags & CLOPS_RENEW)) {
		clp->lc_expiry = nfsrv_leaseexpiry();
	}
	if (opflags & CLOPS_CONFIRM) {
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();
	} else if (opflags != CLOPS_RENEW) {
		NFSUNLOCKSTATE();
	}
	if (clpp)
		*clpp = clp;

out:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Perform the NFSv4.1 destroy clientid.
 */
int
nfsrv_destroyclient(nfsquad_t clientid, NFSPROC_T *p)
{
	struct nfsclient *clp;
	struct nfsclienthashhead *hp;
	int error = 0, i, igotlock;

	if (nfsrvboottime != clientid.lval[0]) {
		error = NFSERR_STALECLIENTID;
		goto out;
	}

	/* Lock out other nfsd threads */
	NFSLOCKV4ROOTMUTEX();
	nfsv4_relref(&nfsv4rootfs_lock);
	do {
		igotlock = nfsv4_lock(&nfsv4rootfs_lock, 1, NULL,
		    NFSV4ROOTLOCKMUTEXPTR, NULL);
	} while (igotlock == 0);
	NFSUNLOCKV4ROOTMUTEX();

	hp = NFSCLIENTHASH(clientid);
	LIST_FOREACH(clp, hp, lc_hash) {
		if (clp->lc_clientid.lval[1] == clientid.lval[1])
			break;
	}
	if (clp == NULL) {
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();
		/* Just return ok, since it is gone. */
		goto out;
	}

	/*
	 * Free up all layouts on the clientid.  Should the client return the
	 * layouts?
	 */
	nfsrv_freelayoutlist(clientid);

	/* Scan for state on the clientid. */
	for (i = 0; i < nfsrv_statehashsize; i++)
		if (!LIST_EMPTY(&clp->lc_stateid[i])) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
			error = NFSERR_CLIENTIDBUSY;
			goto out;
		}
	if (!LIST_EMPTY(&clp->lc_session) || !LIST_EMPTY(&clp->lc_deleg)) {
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();
		error = NFSERR_CLIENTIDBUSY;
		goto out;
	}

	/* Destroy the clientid and return ok. */
	nfsrv_cleanclient(clp, p);
	nfsrv_freedeleglist(&clp->lc_deleg);
	nfsrv_freedeleglist(&clp->lc_olddeleg);
	LIST_REMOVE(clp, lc_hash);
	NFSLOCKV4ROOTMUTEX();
	nfsv4_unlock(&nfsv4rootfs_lock, 1);
	NFSUNLOCKV4ROOTMUTEX();
	nfsrv_zapclient(clp, p);
out:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Called from the new nfssvc syscall to admin revoke a clientid.
 * Returns 0 for success, error otherwise.
 */
APPLESTATIC int
nfsrv_adminrevoke(struct nfsd_clid *revokep, NFSPROC_T *p)
{
	struct nfsclient *clp = NULL;
	int i, error = 0;
	int gotit, igotlock;

	/*
	 * First, lock out the nfsd so that state won't change while the
	 * revocation record is being written to the stable storage restart
	 * file.
	 */
	NFSLOCKV4ROOTMUTEX();
	do {
		igotlock = nfsv4_lock(&nfsv4rootfs_lock, 1, NULL,
		    NFSV4ROOTLOCKMUTEXPTR, NULL);
	} while (!igotlock);
	NFSUNLOCKV4ROOTMUTEX();

	/*
	 * Search for a match in the client list.
	 */
	gotit = i = 0;
	while (i < nfsrv_clienthashsize && !gotit) {
	    LIST_FOREACH(clp, &nfsclienthash[i], lc_hash) {
		if (revokep->nclid_idlen == clp->lc_idlen &&
		    !NFSBCMP(revokep->nclid_id, clp->lc_id, clp->lc_idlen)) {
			gotit = 1;
			break;
		}
	    }
	    i++;
	}
	if (!gotit) {
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 0);
		NFSUNLOCKV4ROOTMUTEX();
		error = EPERM;
		goto out;
	}

	/*
	 * Now, write out the revocation record
	 */
	nfsrv_writestable(clp->lc_id, clp->lc_idlen, NFSNST_REVOKE, p);
	nfsrv_backupstable();

	/*
	 * and clear out the state, marking the clientid revoked.
	 */
	clp->lc_flags &= ~LCL_CALLBACKSON;
	clp->lc_flags |= LCL_ADMINREVOKED;
	nfsrv_cleanclient(clp, p);
	nfsrv_freedeleglist(&clp->lc_deleg);
	nfsrv_freedeleglist(&clp->lc_olddeleg);
	NFSLOCKV4ROOTMUTEX();
	nfsv4_unlock(&nfsv4rootfs_lock, 0);
	NFSUNLOCKV4ROOTMUTEX();

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Dump out stats for all clients. Called from nfssvc(2), that is used
 * nfsstatsv1.
 */
APPLESTATIC void
nfsrv_dumpclients(struct nfsd_dumpclients *dumpp, int maxcnt)
{
	struct nfsclient *clp;
	int i = 0, cnt = 0;

	/*
	 * First, get a reference on the nfsv4rootfs_lock so that an
	 * exclusive lock cannot be acquired while dumping the clients.
	 */
	NFSLOCKV4ROOTMUTEX();
	nfsv4_getref(&nfsv4rootfs_lock, NULL, NFSV4ROOTLOCKMUTEXPTR, NULL);
	NFSUNLOCKV4ROOTMUTEX();
	NFSLOCKSTATE();
	/*
	 * Rattle through the client lists until done.
	 */
	while (i < nfsrv_clienthashsize && cnt < maxcnt) {
	    clp = LIST_FIRST(&nfsclienthash[i]);
	    while (clp != LIST_END(&nfsclienthash[i]) && cnt < maxcnt) {
		nfsrv_dumpaclient(clp, &dumpp[cnt]);
		cnt++;
		clp = LIST_NEXT(clp, lc_hash);
	    }
	    i++;
	}
	if (cnt < maxcnt)
	    dumpp[cnt].ndcl_clid.nclid_idlen = 0;
	NFSUNLOCKSTATE();
	NFSLOCKV4ROOTMUTEX();
	nfsv4_relref(&nfsv4rootfs_lock);
	NFSUNLOCKV4ROOTMUTEX();
}

/*
 * Dump stats for a client. Must be called with the NFSSTATELOCK and spl'd.
 */
static void
nfsrv_dumpaclient(struct nfsclient *clp, struct nfsd_dumpclients *dumpp)
{
	struct nfsstate *stp, *openstp, *lckownstp;
	struct nfslock *lop;
	struct sockaddr *sad;
	struct sockaddr_in *rad;
	struct sockaddr_in6 *rad6;

	dumpp->ndcl_nopenowners = dumpp->ndcl_nlockowners = 0;
	dumpp->ndcl_nopens = dumpp->ndcl_nlocks = 0;
	dumpp->ndcl_ndelegs = dumpp->ndcl_nolddelegs = 0;
	dumpp->ndcl_flags = clp->lc_flags;
	dumpp->ndcl_clid.nclid_idlen = clp->lc_idlen;
	NFSBCOPY(clp->lc_id, dumpp->ndcl_clid.nclid_id, clp->lc_idlen);
	sad = NFSSOCKADDR(clp->lc_req.nr_nam, struct sockaddr *);
	dumpp->ndcl_addrfam = sad->sa_family;
	if (sad->sa_family == AF_INET) {
		rad = (struct sockaddr_in *)sad;
		dumpp->ndcl_cbaddr.sin_addr = rad->sin_addr;
	} else {
		rad6 = (struct sockaddr_in6 *)sad;
		dumpp->ndcl_cbaddr.sin6_addr = rad6->sin6_addr;
	}

	/*
	 * Now, scan the state lists and total up the opens and locks.
	 */
	LIST_FOREACH(stp, &clp->lc_open, ls_list) {
	    dumpp->ndcl_nopenowners++;
	    LIST_FOREACH(openstp, &stp->ls_open, ls_list) {
		dumpp->ndcl_nopens++;
		LIST_FOREACH(lckownstp, &openstp->ls_open, ls_list) {
		    dumpp->ndcl_nlockowners++;
		    LIST_FOREACH(lop, &lckownstp->ls_lock, lo_lckowner) {
			dumpp->ndcl_nlocks++;
		    }
		}
	    }
	}

	/*
	 * and the delegation lists.
	 */
	LIST_FOREACH(stp, &clp->lc_deleg, ls_list) {
	    dumpp->ndcl_ndelegs++;
	}
	LIST_FOREACH(stp, &clp->lc_olddeleg, ls_list) {
	    dumpp->ndcl_nolddelegs++;
	}
}

/*
 * Dump out lock stats for a file.
 */
APPLESTATIC void
nfsrv_dumplocks(vnode_t vp, struct nfsd_dumplocks *ldumpp, int maxcnt,
    NFSPROC_T *p)
{
	struct nfsstate *stp;
	struct nfslock *lop;
	int cnt = 0;
	struct nfslockfile *lfp;
	struct sockaddr *sad;
	struct sockaddr_in *rad;
	struct sockaddr_in6 *rad6;
	int ret;
	fhandle_t nfh;

	ret = nfsrv_getlockfh(vp, 0, NULL, &nfh, p);
	/*
	 * First, get a reference on the nfsv4rootfs_lock so that an
	 * exclusive lock on it cannot be acquired while dumping the locks.
	 */
	NFSLOCKV4ROOTMUTEX();
	nfsv4_getref(&nfsv4rootfs_lock, NULL, NFSV4ROOTLOCKMUTEXPTR, NULL);
	NFSUNLOCKV4ROOTMUTEX();
	NFSLOCKSTATE();
	if (!ret)
		ret = nfsrv_getlockfile(0, NULL, &lfp, &nfh, 0);
	if (ret) {
		ldumpp[0].ndlck_clid.nclid_idlen = 0;
		NFSUNLOCKSTATE();
		NFSLOCKV4ROOTMUTEX();
		nfsv4_relref(&nfsv4rootfs_lock);
		NFSUNLOCKV4ROOTMUTEX();
		return;
	}

	/*
	 * For each open share on file, dump it out.
	 */
	stp = LIST_FIRST(&lfp->lf_open);
	while (stp != LIST_END(&lfp->lf_open) && cnt < maxcnt) {
		ldumpp[cnt].ndlck_flags = stp->ls_flags;
		ldumpp[cnt].ndlck_stateid.seqid = stp->ls_stateid.seqid;
		ldumpp[cnt].ndlck_stateid.other[0] = stp->ls_stateid.other[0];
		ldumpp[cnt].ndlck_stateid.other[1] = stp->ls_stateid.other[1];
		ldumpp[cnt].ndlck_stateid.other[2] = stp->ls_stateid.other[2];
		ldumpp[cnt].ndlck_owner.nclid_idlen =
		    stp->ls_openowner->ls_ownerlen;
		NFSBCOPY(stp->ls_openowner->ls_owner,
		    ldumpp[cnt].ndlck_owner.nclid_id,
		    stp->ls_openowner->ls_ownerlen);
		ldumpp[cnt].ndlck_clid.nclid_idlen = stp->ls_clp->lc_idlen;
		NFSBCOPY(stp->ls_clp->lc_id, ldumpp[cnt].ndlck_clid.nclid_id,
		    stp->ls_clp->lc_idlen);
		sad=NFSSOCKADDR(stp->ls_clp->lc_req.nr_nam, struct sockaddr *);
		ldumpp[cnt].ndlck_addrfam = sad->sa_family;
		if (sad->sa_family == AF_INET) {
			rad = (struct sockaddr_in *)sad;
			ldumpp[cnt].ndlck_cbaddr.sin_addr = rad->sin_addr;
		} else {
			rad6 = (struct sockaddr_in6 *)sad;
			ldumpp[cnt].ndlck_cbaddr.sin6_addr = rad6->sin6_addr;
		}
		stp = LIST_NEXT(stp, ls_file);
		cnt++;
	}

	/*
	 * and all locks.
	 */
	lop = LIST_FIRST(&lfp->lf_lock);
	while (lop != LIST_END(&lfp->lf_lock) && cnt < maxcnt) {
		stp = lop->lo_stp;
		ldumpp[cnt].ndlck_flags = lop->lo_flags;
		ldumpp[cnt].ndlck_first = lop->lo_first;
		ldumpp[cnt].ndlck_end = lop->lo_end;
		ldumpp[cnt].ndlck_stateid.seqid = stp->ls_stateid.seqid;
		ldumpp[cnt].ndlck_stateid.other[0] = stp->ls_stateid.other[0];
		ldumpp[cnt].ndlck_stateid.other[1] = stp->ls_stateid.other[1];
		ldumpp[cnt].ndlck_stateid.other[2] = stp->ls_stateid.other[2];
		ldumpp[cnt].ndlck_owner.nclid_idlen = stp->ls_ownerlen;
		NFSBCOPY(stp->ls_owner, ldumpp[cnt].ndlck_owner.nclid_id,
		    stp->ls_ownerlen);
		ldumpp[cnt].ndlck_clid.nclid_idlen = stp->ls_clp->lc_idlen;
		NFSBCOPY(stp->ls_clp->lc_id, ldumpp[cnt].ndlck_clid.nclid_id,
		    stp->ls_clp->lc_idlen);
		sad=NFSSOCKADDR(stp->ls_clp->lc_req.nr_nam, struct sockaddr *);
		ldumpp[cnt].ndlck_addrfam = sad->sa_family;
		if (sad->sa_family == AF_INET) {
			rad = (struct sockaddr_in *)sad;
			ldumpp[cnt].ndlck_cbaddr.sin_addr = rad->sin_addr;
		} else {
			rad6 = (struct sockaddr_in6 *)sad;
			ldumpp[cnt].ndlck_cbaddr.sin6_addr = rad6->sin6_addr;
		}
		lop = LIST_NEXT(lop, lo_lckfile);
		cnt++;
	}

	/*
	 * and the delegations.
	 */
	stp = LIST_FIRST(&lfp->lf_deleg);
	while (stp != LIST_END(&lfp->lf_deleg) && cnt < maxcnt) {
		ldumpp[cnt].ndlck_flags = stp->ls_flags;
		ldumpp[cnt].ndlck_stateid.seqid = stp->ls_stateid.seqid;
		ldumpp[cnt].ndlck_stateid.other[0] = stp->ls_stateid.other[0];
		ldumpp[cnt].ndlck_stateid.other[1] = stp->ls_stateid.other[1];
		ldumpp[cnt].ndlck_stateid.other[2] = stp->ls_stateid.other[2];
		ldumpp[cnt].ndlck_owner.nclid_idlen = 0;
		ldumpp[cnt].ndlck_clid.nclid_idlen = stp->ls_clp->lc_idlen;
		NFSBCOPY(stp->ls_clp->lc_id, ldumpp[cnt].ndlck_clid.nclid_id,
		    stp->ls_clp->lc_idlen);
		sad=NFSSOCKADDR(stp->ls_clp->lc_req.nr_nam, struct sockaddr *);
		ldumpp[cnt].ndlck_addrfam = sad->sa_family;
		if (sad->sa_family == AF_INET) {
			rad = (struct sockaddr_in *)sad;
			ldumpp[cnt].ndlck_cbaddr.sin_addr = rad->sin_addr;
		} else {
			rad6 = (struct sockaddr_in6 *)sad;
			ldumpp[cnt].ndlck_cbaddr.sin6_addr = rad6->sin6_addr;
		}
		stp = LIST_NEXT(stp, ls_file);
		cnt++;
	}

	/*
	 * If list isn't full, mark end of list by setting the client name
	 * to zero length.
	 */
	if (cnt < maxcnt)
		ldumpp[cnt].ndlck_clid.nclid_idlen = 0;
	NFSUNLOCKSTATE();
	NFSLOCKV4ROOTMUTEX();
	nfsv4_relref(&nfsv4rootfs_lock);
	NFSUNLOCKV4ROOTMUTEX();
}

/*
 * Server timer routine. It can scan any linked list, so long
 * as it holds the spin/mutex lock and there is no exclusive lock on
 * nfsv4rootfs_lock.
 * (For OpenBSD, a kthread is ok. For FreeBSD, I think it is ok
 *  to do this from a callout, since the spin locks work. For
 *  Darwin, I'm not sure what will work correctly yet.)
 * Should be called once per second.
 */
APPLESTATIC void
nfsrv_servertimer(void)
{
	struct nfsclient *clp, *nclp;
	struct nfsstate *stp, *nstp;
	int got_ref, i;

	/*
	 * Make sure nfsboottime is set. This is used by V3 as well
	 * as V4. Note that nfsboottime is not nfsrvboottime, which is
	 * only used by the V4 server for leases.
	 */
	if (nfsboottime.tv_sec == 0)
		NFSSETBOOTTIME(nfsboottime);

	/*
	 * If server hasn't started yet, just return.
	 */
	NFSLOCKSTATE();
	if (nfsrv_stablefirst.nsf_eograce == 0) {
		NFSUNLOCKSTATE();
		return;
	}
	if (!(nfsrv_stablefirst.nsf_flags & NFSNSF_UPDATEDONE)) {
		if (!(nfsrv_stablefirst.nsf_flags & NFSNSF_GRACEOVER) &&
		    NFSD_MONOSEC > nfsrv_stablefirst.nsf_eograce)
			nfsrv_stablefirst.nsf_flags |=
			    (NFSNSF_GRACEOVER | NFSNSF_NEEDLOCK);
		NFSUNLOCKSTATE();
		return;
	}

	/*
	 * Try and get a reference count on the nfsv4rootfs_lock so that
	 * no nfsd thread can acquire an exclusive lock on it before this
	 * call is done. If it is already exclusively locked, just return.
	 */
	NFSLOCKV4ROOTMUTEX();
	got_ref = nfsv4_getref_nonblock(&nfsv4rootfs_lock);
	NFSUNLOCKV4ROOTMUTEX();
	if (got_ref == 0) {
		NFSUNLOCKSTATE();
		return;
	}

	/*
	 * For each client...
	 */
	for (i = 0; i < nfsrv_clienthashsize; i++) {
	    clp = LIST_FIRST(&nfsclienthash[i]);
	    while (clp != LIST_END(&nfsclienthash[i])) {
		nclp = LIST_NEXT(clp, lc_hash);
		if (!(clp->lc_flags & LCL_EXPIREIT)) {
		    if (((clp->lc_expiry + NFSRV_STALELEASE) < NFSD_MONOSEC
			 && ((LIST_EMPTY(&clp->lc_deleg)
			      && LIST_EMPTY(&clp->lc_open)) ||
			     nfsrv_clients > nfsrv_clienthighwater)) ||
			(clp->lc_expiry + NFSRV_MOULDYLEASE) < NFSD_MONOSEC ||
			(clp->lc_expiry < NFSD_MONOSEC &&
			 (nfsrv_openpluslock * 10 / 9) > nfsrv_v4statelimit)) {
			/*
			 * Lease has expired several nfsrv_lease times ago:
			 * PLUS
			 *    - no state is associated with it
			 *    OR
			 *    - above high water mark for number of clients
			 *      (nfsrv_clienthighwater should be large enough
			 *       that this only occurs when clients fail to
			 *       use the same nfs_client_id4.id. Maybe somewhat
			 *       higher that the maximum number of clients that
			 *       will mount this server?)
			 * OR
			 * Lease has expired a very long time ago
			 * OR
			 * Lease has expired PLUS the number of opens + locks
			 * has exceeded 90% of capacity
			 *
			 * --> Mark for expiry. The actual expiry will be done
			 *     by an nfsd sometime soon.
			 */
			clp->lc_flags |= LCL_EXPIREIT;
			nfsrv_stablefirst.nsf_flags |=
			    (NFSNSF_NEEDLOCK | NFSNSF_EXPIREDCLIENT);
		    } else {
			/*
			 * If there are no opens, increment no open tick cnt
			 * If time exceeds NFSNOOPEN, mark it to be thrown away
			 * otherwise, if there is an open, reset no open time
			 * Hopefully, this will avoid excessive re-creation
			 * of open owners and subsequent open confirms.
			 */
			stp = LIST_FIRST(&clp->lc_open);
			while (stp != LIST_END(&clp->lc_open)) {
				nstp = LIST_NEXT(stp, ls_list);
				if (LIST_EMPTY(&stp->ls_open)) {
					stp->ls_noopens++;
					if (stp->ls_noopens > NFSNOOPEN ||
					    (nfsrv_openpluslock * 2) >
					    nfsrv_v4statelimit)
						nfsrv_stablefirst.nsf_flags |=
							NFSNSF_NOOPENS;
				} else {
					stp->ls_noopens = 0;
				}
				stp = nstp;
			}
		    }
		}
		clp = nclp;
	    }
	}
	NFSUNLOCKSTATE();
	NFSLOCKV4ROOTMUTEX();
	nfsv4_relref(&nfsv4rootfs_lock);
	NFSUNLOCKV4ROOTMUTEX();
}

/*
 * The following set of functions free up the various data structures.
 */
/*
 * Clear out all open/lock state related to this nfsclient.
 * Caller must hold an exclusive lock on nfsv4rootfs_lock, so that
 * there are no other active nfsd threads.
 */
APPLESTATIC void
nfsrv_cleanclient(struct nfsclient *clp, NFSPROC_T *p)
{
	struct nfsstate *stp, *nstp;
	struct nfsdsession *sep, *nsep;

	LIST_FOREACH_SAFE(stp, &clp->lc_open, ls_list, nstp)
		nfsrv_freeopenowner(stp, 1, p);
	if ((clp->lc_flags & LCL_ADMINREVOKED) == 0)
		LIST_FOREACH_SAFE(sep, &clp->lc_session, sess_list, nsep)
			(void)nfsrv_freesession(sep, NULL);
}

/*
 * Free a client that has been cleaned. It should also already have been
 * removed from the lists.
 * (Just to be safe w.r.t. newnfs_disconnect(), call this function when
 *  softclock interrupts are enabled.)
 */
APPLESTATIC void
nfsrv_zapclient(struct nfsclient *clp, NFSPROC_T *p)
{

#ifdef notyet
	if ((clp->lc_flags & (LCL_GSS | LCL_CALLBACKSON)) ==
	     (LCL_GSS | LCL_CALLBACKSON) &&
	    (clp->lc_hand.nfsh_flag & NFSG_COMPLETE) &&
	    clp->lc_handlelen > 0) {
		clp->lc_hand.nfsh_flag &= ~NFSG_COMPLETE;
		clp->lc_hand.nfsh_flag |= NFSG_DESTROYED;
		(void) nfsrv_docallback(clp, NFSV4PROC_CBNULL,
			NULL, 0, NULL, NULL, NULL, 0, p);
	}
#endif
	newnfs_disconnect(&clp->lc_req);
	free(clp->lc_req.nr_nam, M_SONAME);
	NFSFREEMUTEX(&clp->lc_req.nr_mtx);
	free(clp->lc_stateid, M_NFSDCLIENT);
	free(clp, M_NFSDCLIENT);
	NFSLOCKSTATE();
	nfsstatsv1.srvclients--;
	nfsrv_openpluslock--;
	nfsrv_clients--;
	NFSUNLOCKSTATE();
}

/*
 * Free a list of delegation state structures.
 * (This function will also free all nfslockfile structures that no
 *  longer have associated state.)
 */
APPLESTATIC void
nfsrv_freedeleglist(struct nfsstatehead *sthp)
{
	struct nfsstate *stp, *nstp;

	LIST_FOREACH_SAFE(stp, sthp, ls_list, nstp) {
		nfsrv_freedeleg(stp);
	}
	LIST_INIT(sthp);
}

/*
 * Free up a delegation.
 */
static void
nfsrv_freedeleg(struct nfsstate *stp)
{
	struct nfslockfile *lfp;

	LIST_REMOVE(stp, ls_hash);
	LIST_REMOVE(stp, ls_list);
	LIST_REMOVE(stp, ls_file);
	if ((stp->ls_flags & NFSLCK_DELEGWRITE) != 0)
		nfsrv_writedelegcnt--;
	lfp = stp->ls_lfp;
	if (LIST_EMPTY(&lfp->lf_open) &&
	    LIST_EMPTY(&lfp->lf_lock) && LIST_EMPTY(&lfp->lf_deleg) &&
	    LIST_EMPTY(&lfp->lf_locallock) && LIST_EMPTY(&lfp->lf_rollback) &&
	    lfp->lf_usecount == 0 &&
	    nfsv4_testlock(&lfp->lf_locallock_lck) == 0)
		nfsrv_freenfslockfile(lfp);
	free(stp, M_NFSDSTATE);
	nfsstatsv1.srvdelegates--;
	nfsrv_openpluslock--;
	nfsrv_delegatecnt--;
}

/*
 * This function frees an open owner and all associated opens.
 */
static void
nfsrv_freeopenowner(struct nfsstate *stp, int cansleep, NFSPROC_T *p)
{
	struct nfsstate *nstp, *tstp;

	LIST_REMOVE(stp, ls_list);
	/*
	 * Now, free all associated opens.
	 */
	nstp = LIST_FIRST(&stp->ls_open);
	while (nstp != LIST_END(&stp->ls_open)) {
		tstp = nstp;
		nstp = LIST_NEXT(nstp, ls_list);
		(void) nfsrv_freeopen(tstp, NULL, cansleep, p);
	}
	if (stp->ls_op)
		nfsrvd_derefcache(stp->ls_op);
	free(stp, M_NFSDSTATE);
	nfsstatsv1.srvopenowners--;
	nfsrv_openpluslock--;
}

/*
 * This function frees an open (nfsstate open structure) with all associated
 * lock_owners and locks. It also frees the nfslockfile structure iff there
 * are no other opens on the file.
 * Returns 1 if it free'd the nfslockfile, 0 otherwise.
 */
static int
nfsrv_freeopen(struct nfsstate *stp, vnode_t vp, int cansleep, NFSPROC_T *p)
{
	struct nfsstate *nstp, *tstp;
	struct nfslockfile *lfp;
	int ret;

	LIST_REMOVE(stp, ls_hash);
	LIST_REMOVE(stp, ls_list);
	LIST_REMOVE(stp, ls_file);

	lfp = stp->ls_lfp;
	/*
	 * Now, free all lockowners associated with this open.
	 */
	LIST_FOREACH_SAFE(tstp, &stp->ls_open, ls_list, nstp)
		nfsrv_freelockowner(tstp, vp, cansleep, p);

	/*
	 * The nfslockfile is freed here if there are no locks
	 * associated with the open.
	 * If there are locks associated with the open, the
	 * nfslockfile structure can be freed via nfsrv_freelockowner().
	 * Acquire the state mutex to avoid races with calls to
	 * nfsrv_getlockfile().
	 */
	if (cansleep != 0)
		NFSLOCKSTATE();
	if (lfp != NULL && LIST_EMPTY(&lfp->lf_open) &&
	    LIST_EMPTY(&lfp->lf_deleg) && LIST_EMPTY(&lfp->lf_lock) &&
	    LIST_EMPTY(&lfp->lf_locallock) && LIST_EMPTY(&lfp->lf_rollback) &&
	    lfp->lf_usecount == 0 &&
	    (cansleep != 0 || nfsv4_testlock(&lfp->lf_locallock_lck) == 0)) {
		nfsrv_freenfslockfile(lfp);
		ret = 1;
	} else
		ret = 0;
	if (cansleep != 0)
		NFSUNLOCKSTATE();
	free(stp, M_NFSDSTATE);
	nfsstatsv1.srvopens--;
	nfsrv_openpluslock--;
	return (ret);
}

/*
 * Frees a lockowner and all associated locks.
 */
static void
nfsrv_freelockowner(struct nfsstate *stp, vnode_t vp, int cansleep,
    NFSPROC_T *p)
{

	LIST_REMOVE(stp, ls_hash);
	LIST_REMOVE(stp, ls_list);
	nfsrv_freeallnfslocks(stp, vp, cansleep, p);
	if (stp->ls_op)
		nfsrvd_derefcache(stp->ls_op);
	free(stp, M_NFSDSTATE);
	nfsstatsv1.srvlockowners--;
	nfsrv_openpluslock--;
}

/*
 * Free all the nfs locks on a lockowner.
 */
static void
nfsrv_freeallnfslocks(struct nfsstate *stp, vnode_t vp, int cansleep,
    NFSPROC_T *p)
{
	struct nfslock *lop, *nlop;
	struct nfsrollback *rlp, *nrlp;
	struct nfslockfile *lfp = NULL;
	int gottvp = 0;
	vnode_t tvp = NULL;
	uint64_t first, end;

	if (vp != NULL)
		ASSERT_VOP_UNLOCKED(vp, "nfsrv_freeallnfslocks: vnode locked");
	lop = LIST_FIRST(&stp->ls_lock);
	while (lop != LIST_END(&stp->ls_lock)) {
		nlop = LIST_NEXT(lop, lo_lckowner);
		/*
		 * Since all locks should be for the same file, lfp should
		 * not change.
		 */
		if (lfp == NULL)
			lfp = lop->lo_lfp;
		else if (lfp != lop->lo_lfp)
			panic("allnfslocks");
		/*
		 * If vp is NULL and cansleep != 0, a vnode must be acquired
		 * from the file handle. This only occurs when called from
		 * nfsrv_cleanclient().
		 */
		if (gottvp == 0) {
			if (nfsrv_dolocallocks == 0)
				tvp = NULL;
			else if (vp == NULL && cansleep != 0) {
				tvp = nfsvno_getvp(&lfp->lf_fh);
				NFSVOPUNLOCK(tvp, 0);
			} else
				tvp = vp;
			gottvp = 1;
		}

		if (tvp != NULL) {
			if (cansleep == 0)
				panic("allnfs2");
			first = lop->lo_first;
			end = lop->lo_end;
			nfsrv_freenfslock(lop);
			nfsrv_localunlock(tvp, lfp, first, end, p);
			LIST_FOREACH_SAFE(rlp, &lfp->lf_rollback, rlck_list,
			    nrlp)
				free(rlp, M_NFSDROLLBACK);
			LIST_INIT(&lfp->lf_rollback);
		} else
			nfsrv_freenfslock(lop);
		lop = nlop;
	}
	if (vp == NULL && tvp != NULL)
		vrele(tvp);
}

/*
 * Free an nfslock structure.
 */
static void
nfsrv_freenfslock(struct nfslock *lop)
{

	if (lop->lo_lckfile.le_prev != NULL) {
		LIST_REMOVE(lop, lo_lckfile);
		nfsstatsv1.srvlocks--;
		nfsrv_openpluslock--;
	}
	LIST_REMOVE(lop, lo_lckowner);
	free(lop, M_NFSDLOCK);
}

/*
 * This function frees an nfslockfile structure.
 */
static void
nfsrv_freenfslockfile(struct nfslockfile *lfp)
{

	LIST_REMOVE(lfp, lf_hash);
	free(lfp, M_NFSDLOCKFILE);
}

/*
 * This function looks up an nfsstate structure via stateid.
 */
static int
nfsrv_getstate(struct nfsclient *clp, nfsv4stateid_t *stateidp, __unused u_int32_t flags,
    struct nfsstate **stpp)
{
	struct nfsstate *stp;
	struct nfsstatehead *hp;
	int error = 0;

	*stpp = NULL;
	hp = NFSSTATEHASH(clp, *stateidp);
	LIST_FOREACH(stp, hp, ls_hash) {
		if (!NFSBCMP(stp->ls_stateid.other, stateidp->other,
			NFSX_STATEIDOTHER))
			break;
	}

	/*
	 * If no state id in list, return NFSERR_BADSTATEID.
	 */
	if (stp == LIST_END(hp)) {
		error = NFSERR_BADSTATEID;
		goto out;
	}
	*stpp = stp;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * This function gets an nfsstate structure via owner string.
 */
static void
nfsrv_getowner(struct nfsstatehead *hp, struct nfsstate *new_stp,
    struct nfsstate **stpp)
{
	struct nfsstate *stp;

	*stpp = NULL;
	LIST_FOREACH(stp, hp, ls_list) {
		if (new_stp->ls_ownerlen == stp->ls_ownerlen &&
		  !NFSBCMP(new_stp->ls_owner,stp->ls_owner,stp->ls_ownerlen)) {
			*stpp = stp;
			return;
		}
	}
}

/*
 * Lock control function called to update lock status.
 * Returns 0 upon success, -1 if there is no lock and the flags indicate
 * that one isn't to be created and an NFSERR_xxx for other errors.
 * The structures new_stp and new_lop are passed in as pointers that should
 * be set to NULL if the structure is used and shouldn't be free'd.
 * For the NFSLCK_TEST and NFSLCK_CHECK cases, the structures are
 * never used and can safely be allocated on the stack. For all other
 * cases, *new_stpp and *new_lopp should be malloc'd before the call,
 * in case they are used.
 */
APPLESTATIC int
nfsrv_lockctrl(vnode_t vp, struct nfsstate **new_stpp,
    struct nfslock **new_lopp, struct nfslockconflict *cfp,
    nfsquad_t clientid, nfsv4stateid_t *stateidp,
    __unused struct nfsexstuff *exp,
    struct nfsrv_descript *nd, NFSPROC_T *p)
{
	struct nfslock *lop;
	struct nfsstate *new_stp = *new_stpp;
	struct nfslock *new_lop = *new_lopp;
	struct nfsstate *tstp, *mystp, *nstp;
	int specialid = 0;
	struct nfslockfile *lfp;
	struct nfslock *other_lop = NULL;
	struct nfsstate *stp, *lckstp = NULL;
	struct nfsclient *clp = NULL;
	u_int32_t bits;
	int error = 0, haslock = 0, ret, reterr;
	int getlckret, delegation = 0, filestruct_locked, vnode_unlocked = 0;
	fhandle_t nfh;
	uint64_t first, end;
	uint32_t lock_flags;

	if (new_stp->ls_flags & (NFSLCK_CHECK | NFSLCK_SETATTR)) {
		/*
		 * Note the special cases of "all 1s" or "all 0s" stateids and
		 * let reads with all 1s go ahead.
		 */
		if (new_stp->ls_stateid.seqid == 0x0 &&
		    new_stp->ls_stateid.other[0] == 0x0 &&
		    new_stp->ls_stateid.other[1] == 0x0 &&
		    new_stp->ls_stateid.other[2] == 0x0)
			specialid = 1;
		else if (new_stp->ls_stateid.seqid == 0xffffffff &&
		    new_stp->ls_stateid.other[0] == 0xffffffff &&
		    new_stp->ls_stateid.other[1] == 0xffffffff &&
		    new_stp->ls_stateid.other[2] == 0xffffffff)
			specialid = 2;
	}

	/*
	 * Check for restart conditions (client and server).
	 */
	error = nfsrv_checkrestart(clientid, new_stp->ls_flags,
	    &new_stp->ls_stateid, specialid);
	if (error)
		goto out;

	/*
	 * Check for state resource limit exceeded.
	 */
	if ((new_stp->ls_flags & NFSLCK_LOCK) &&
	    nfsrv_openpluslock > nfsrv_v4statelimit) {
		error = NFSERR_RESOURCE;
		goto out;
	}

	/*
	 * For the lock case, get another nfslock structure,
	 * just in case we need it.
	 * Malloc now, before we start sifting through the linked lists,
	 * in case we have to wait for memory.
	 */
tryagain:
	if (new_stp->ls_flags & NFSLCK_LOCK)
		other_lop = malloc(sizeof (struct nfslock),
		    M_NFSDLOCK, M_WAITOK);
	filestruct_locked = 0;
	reterr = 0;
	lfp = NULL;

	/*
	 * Get the lockfile structure for CFH now, so we can do a sanity
	 * check against the stateid, before incrementing the seqid#, since
	 * we want to return NFSERR_BADSTATEID on failure and the seqid#
	 * shouldn't be incremented for this case.
	 * If nfsrv_getlockfile() returns -1, it means "not found", which
	 * will be handled later.
	 * If we are doing Lock/LockU and local locking is enabled, sleep
	 * lock the nfslockfile structure.
	 */
	getlckret = nfsrv_getlockfh(vp, new_stp->ls_flags, NULL, &nfh, p);
	NFSLOCKSTATE();
	if (getlckret == 0) {
		if ((new_stp->ls_flags & (NFSLCK_LOCK | NFSLCK_UNLOCK)) != 0 &&
		    nfsrv_dolocallocks != 0 && nd->nd_repstat == 0) {
			getlckret = nfsrv_getlockfile(new_stp->ls_flags, NULL,
			    &lfp, &nfh, 1);
			if (getlckret == 0)
				filestruct_locked = 1;
		} else
			getlckret = nfsrv_getlockfile(new_stp->ls_flags, NULL,
			    &lfp, &nfh, 0);
	}
	if (getlckret != 0 && getlckret != -1)
		reterr = getlckret;

	if (filestruct_locked != 0) {
		LIST_INIT(&lfp->lf_rollback);
		if ((new_stp->ls_flags & NFSLCK_LOCK)) {
			/*
			 * For local locking, do the advisory locking now, so
			 * that any conflict can be detected. A failure later
			 * can be rolled back locally. If an error is returned,
			 * struct nfslockfile has been unlocked and any local
			 * locking rolled back.
			 */
			NFSUNLOCKSTATE();
			if (vnode_unlocked == 0) {
				ASSERT_VOP_ELOCKED(vp, "nfsrv_lockctrl1");
				vnode_unlocked = 1;
				NFSVOPUNLOCK(vp, 0);
			}
			reterr = nfsrv_locallock(vp, lfp,
			    (new_lop->lo_flags & (NFSLCK_READ | NFSLCK_WRITE)),
			    new_lop->lo_first, new_lop->lo_end, cfp, p);
			NFSLOCKSTATE();
		}
	}

	if (specialid == 0) {
	    if (new_stp->ls_flags & NFSLCK_TEST) {
		/*
		 * RFC 3530 does not list LockT as an op that renews a
		 * lease, but the consensus seems to be that it is ok
		 * for a server to do so.
		 */
		error = nfsrv_getclient(clientid, CLOPS_RENEW, &clp, NULL,
		    (nfsquad_t)((u_quad_t)0), 0, nd, p);

		/*
		 * Since NFSERR_EXPIRED, NFSERR_ADMINREVOKED are not valid
		 * error returns for LockT, just go ahead and test for a lock,
		 * since there are no locks for this client, but other locks
		 * can conflict. (ie. same client will always be false)
		 */
		if (error == NFSERR_EXPIRED || error == NFSERR_ADMINREVOKED)
		    error = 0;
		lckstp = new_stp;
	    } else {
	      error = nfsrv_getclient(clientid, CLOPS_RENEW, &clp, NULL,
		(nfsquad_t)((u_quad_t)0), 0, nd, p);
	      if (error == 0)
		/*
		 * Look up the stateid
		 */
		error = nfsrv_getstate(clp, &new_stp->ls_stateid,
		  new_stp->ls_flags, &stp);
	      /*
	       * do some sanity checks for an unconfirmed open or a
	       * stateid that refers to the wrong file, for an open stateid
	       */
	      if (error == 0 && (stp->ls_flags & NFSLCK_OPEN) &&
		  ((stp->ls_openowner->ls_flags & NFSLCK_NEEDSCONFIRM) ||
		   (getlckret == 0 && stp->ls_lfp != lfp))){
		      /*
		       * NFSLCK_SETATTR should return OK rather than NFSERR_BADSTATEID
		       * The only exception is using SETATTR with SIZE.
		       * */
                    if ((new_stp->ls_flags &
                         (NFSLCK_SETATTR | NFSLCK_CHECK)) != NFSLCK_SETATTR)
			     error = NFSERR_BADSTATEID;
	      }
	      
		if (error == 0 &&
		  (stp->ls_flags & (NFSLCK_DELEGREAD | NFSLCK_DELEGWRITE)) &&
		  getlckret == 0 && stp->ls_lfp != lfp)
			error = NFSERR_BADSTATEID;

	      /*
	       * If the lockowner stateid doesn't refer to the same file,
	       * I believe that is considered ok, since some clients will
	       * only create a single lockowner and use that for all locks
	       * on all files.
	       * For now, log it as a diagnostic, instead of considering it
	       * a BadStateid.
	       */
	      if (error == 0 && (stp->ls_flags &
		  (NFSLCK_OPEN | NFSLCK_DELEGREAD | NFSLCK_DELEGWRITE)) == 0 &&
		  getlckret == 0 && stp->ls_lfp != lfp) {
#ifdef DIAGNOSTIC
		  printf("Got a lock statid for different file open\n");
#endif
		  /*
		  error = NFSERR_BADSTATEID;
		  */
	      }

	      if (error == 0) {
		    if (new_stp->ls_flags & NFSLCK_OPENTOLOCK) {
			/*
			 * If haslock set, we've already checked the seqid.
			 */
			if (!haslock) {
			    if (stp->ls_flags & NFSLCK_OPEN)
				error = nfsrv_checkseqid(nd, new_stp->ls_seq,
				    stp->ls_openowner, new_stp->ls_op);
			    else
				error = NFSERR_BADSTATEID;
			}
			if (!error)
			    nfsrv_getowner(&stp->ls_open, new_stp, &lckstp);
			if (lckstp)
			    /*
			     * I believe this should be an error, but it
			     * isn't obvious what NFSERR_xxx would be
			     * appropriate, so I'll use NFSERR_INVAL for now.
			     */
			    error = NFSERR_INVAL;
			else
			    lckstp = new_stp;
		    } else if (new_stp->ls_flags&(NFSLCK_LOCK|NFSLCK_UNLOCK)) {
			/*
			 * If haslock set, ditto above.
			 */
			if (!haslock) {
			    if (stp->ls_flags & NFSLCK_OPEN)
				error = NFSERR_BADSTATEID;
			    else
				error = nfsrv_checkseqid(nd, new_stp->ls_seq,
				    stp, new_stp->ls_op);
			}
			lckstp = stp;
		    } else {
			lckstp = stp;
		    }
	      }
	      /*
	       * If the seqid part of the stateid isn't the same, return
	       * NFSERR_OLDSTATEID for cases other than I/O Ops.
	       * For I/O Ops, only return NFSERR_OLDSTATEID if
	       * nfsrv_returnoldstateid is set. (The consensus on the email
	       * list was that most clients would prefer to not receive
	       * NFSERR_OLDSTATEID for I/O Ops, but the RFC suggests that that
	       * is what will happen, so I use the nfsrv_returnoldstateid to
	       * allow for either server configuration.)
	       */
	      if (!error && stp->ls_stateid.seqid!=new_stp->ls_stateid.seqid &&
		  (((nd->nd_flag & ND_NFSV41) == 0 &&
		   (!(new_stp->ls_flags & NFSLCK_CHECK) ||
		    nfsrv_returnoldstateid)) ||
		   ((nd->nd_flag & ND_NFSV41) != 0 &&
		    new_stp->ls_stateid.seqid != 0)))
		    error = NFSERR_OLDSTATEID;
	    }
	}

	/*
	 * Now we can check for grace.
	 */
	if (!error)
		error = nfsrv_checkgrace(nd, clp, new_stp->ls_flags);
	if ((new_stp->ls_flags & NFSLCK_RECLAIM) && !error &&
		nfsrv_checkstable(clp))
		error = NFSERR_NOGRACE;
	/*
	 * If we successfully Reclaimed state, note that.
	 */
	if ((new_stp->ls_flags & NFSLCK_RECLAIM) && !error)
		nfsrv_markstable(clp);

	/*
	 * At this point, either error == NFSERR_BADSTATEID or the
	 * seqid# has been updated, so we can return any error.
	 * If error == 0, there may be an error in:
	 *    nd_repstat - Set by the calling function.
	 *    reterr - Set above, if getting the nfslockfile structure
	 *       or acquiring the local lock failed.
	 *    (If both of these are set, nd_repstat should probably be
	 *     returned, since that error was detected before this
	 *     function call.)
	 */
	if (error != 0 || nd->nd_repstat != 0 || reterr != 0) {
		if (error == 0) {
			if (nd->nd_repstat != 0)
				error = nd->nd_repstat;
			else
				error = reterr;
		}
		if (filestruct_locked != 0) {
			/* Roll back local locks. */
			NFSUNLOCKSTATE();
			if (vnode_unlocked == 0) {
				ASSERT_VOP_ELOCKED(vp, "nfsrv_lockctrl2");
				vnode_unlocked = 1;
				NFSVOPUNLOCK(vp, 0);
			}
			nfsrv_locallock_rollback(vp, lfp, p);
			NFSLOCKSTATE();
			nfsrv_unlocklf(lfp);
		}
		NFSUNLOCKSTATE();
		goto out;
	}

	/*
	 * Check the nfsrv_getlockfile return.
	 * Returned -1 if no structure found.
	 */
	if (getlckret == -1) {
		error = NFSERR_EXPIRED;
		/*
		 * Called from lockt, so no lock is OK.
		 */
		if (new_stp->ls_flags & NFSLCK_TEST) {
			error = 0;
		} else if (new_stp->ls_flags &
		    (NFSLCK_CHECK | NFSLCK_SETATTR)) {
			/*
			 * Called to check for a lock, OK if the stateid is all
			 * 1s or all 0s, but there should be an nfsstate
			 * otherwise.
			 * (ie. If there is no open, I'll assume no share
			 *  deny bits.)
			 */
			if (specialid)
				error = 0;
			else
				error = NFSERR_BADSTATEID;
		}
		NFSUNLOCKSTATE();
		goto out;
	}

	/*
	 * For NFSLCK_CHECK and NFSLCK_LOCK, test for a share conflict.
	 * For NFSLCK_CHECK, allow a read if write access is granted,
	 * but check for a deny. For NFSLCK_LOCK, require correct access,
	 * which implies a conflicting deny can't exist.
	 */
	if (new_stp->ls_flags & (NFSLCK_CHECK | NFSLCK_LOCK)) {
	    /*
	     * Four kinds of state id:
	     * - specialid (all 0s or all 1s), only for NFSLCK_CHECK
	     * - stateid for an open
	     * - stateid for a delegation
	     * - stateid for a lock owner
	     */
	    if (!specialid) {
		if (stp->ls_flags & (NFSLCK_DELEGREAD | NFSLCK_DELEGWRITE)) {
		    delegation = 1;
		    mystp = stp;
		    nfsrv_delaydelegtimeout(stp);
	        } else if (stp->ls_flags & NFSLCK_OPEN) {
		    mystp = stp;
		} else {
		    mystp = stp->ls_openstp;
		}
		/*
		 * If locking or checking, require correct access
		 * bit set.
		 */
		if (((new_stp->ls_flags & NFSLCK_LOCK) &&
		     !((new_lop->lo_flags >> NFSLCK_LOCKSHIFT) &
		       mystp->ls_flags & NFSLCK_ACCESSBITS)) ||
		    ((new_stp->ls_flags & (NFSLCK_CHECK|NFSLCK_READACCESS)) ==
		      (NFSLCK_CHECK | NFSLCK_READACCESS) &&
		     !(mystp->ls_flags & NFSLCK_READACCESS) &&
		     nfsrv_allowreadforwriteopen == 0) ||
		    ((new_stp->ls_flags & (NFSLCK_CHECK|NFSLCK_WRITEACCESS)) ==
		      (NFSLCK_CHECK | NFSLCK_WRITEACCESS) &&
		     !(mystp->ls_flags & NFSLCK_WRITEACCESS))) {
			if (filestruct_locked != 0) {
				/* Roll back local locks. */
				NFSUNLOCKSTATE();
				if (vnode_unlocked == 0) {
					ASSERT_VOP_ELOCKED(vp,
					    "nfsrv_lockctrl3");
					vnode_unlocked = 1;
					NFSVOPUNLOCK(vp, 0);
				}
				nfsrv_locallock_rollback(vp, lfp, p);
				NFSLOCKSTATE();
				nfsrv_unlocklf(lfp);
			}
			NFSUNLOCKSTATE();
			error = NFSERR_OPENMODE;
			goto out;
		}
	    } else
		mystp = NULL;
	    if ((new_stp->ls_flags & NFSLCK_CHECK) && !delegation) {
		/*
		 * Check for a conflicting deny bit.
		 */
		LIST_FOREACH(tstp, &lfp->lf_open, ls_file) {
		    if (tstp != mystp) {
			bits = tstp->ls_flags;
			bits >>= NFSLCK_SHIFT;
			if (new_stp->ls_flags & bits & NFSLCK_ACCESSBITS) {
			    KASSERT(vnode_unlocked == 0,
				("nfsrv_lockctrl: vnode unlocked1"));
			    ret = nfsrv_clientconflict(tstp->ls_clp, &haslock,
				vp, p);
			    if (ret == 1) {
				/*
				* nfsrv_clientconflict unlocks state
				 * when it returns non-zero.
				 */
				lckstp = NULL;
				goto tryagain;
			    }
			    if (ret == 0)
				NFSUNLOCKSTATE();
			    if (ret == 2)
				error = NFSERR_PERM;
			    else
				error = NFSERR_OPENMODE;
			    goto out;
			}
		    }
		}

		/* We're outta here */
		NFSUNLOCKSTATE();
		goto out;
	    }
	}

	/*
	 * For setattr, just get rid of all the Delegations for other clients.
	 */
	if (new_stp->ls_flags & NFSLCK_SETATTR) {
		KASSERT(vnode_unlocked == 0,
		    ("nfsrv_lockctrl: vnode unlocked2"));
		ret = nfsrv_cleandeleg(vp, lfp, clp, &haslock, p);
		if (ret) {
			/*
			 * nfsrv_cleandeleg() unlocks state when it
			 * returns non-zero.
			 */
			if (ret == -1) {
				lckstp = NULL;
				goto tryagain;
			}
			error = ret;
			goto out;
		}
		if (!(new_stp->ls_flags & NFSLCK_CHECK) ||
		    (LIST_EMPTY(&lfp->lf_open) && LIST_EMPTY(&lfp->lf_lock) &&
		     LIST_EMPTY(&lfp->lf_deleg))) {
			NFSUNLOCKSTATE();
			goto out;
		}
	}

	/*
	 * Check for a conflicting delegation. If one is found, call
	 * nfsrv_delegconflict() to handle it. If the v4root lock hasn't
	 * been set yet, it will get the lock. Otherwise, it will recall
	 * the delegation. Then, we try try again...
	 * I currently believe the conflict algorithm to be:
	 * For Lock Ops (Lock/LockT/LockU)
	 * - there is a conflict iff a different client has a write delegation
	 * For Reading (Read Op)
	 * - there is a conflict iff a different client has a write delegation
	 *   (the specialids are always a different client)
	 * For Writing (Write/Setattr of size)
	 * - there is a conflict if a different client has any delegation
	 * - there is a conflict if the same client has a read delegation
	 *   (I don't understand why this isn't allowed, but that seems to be
	 *    the current consensus?)
	 */
	tstp = LIST_FIRST(&lfp->lf_deleg);
	while (tstp != LIST_END(&lfp->lf_deleg)) {
	    nstp = LIST_NEXT(tstp, ls_file);
	    if ((((new_stp->ls_flags&(NFSLCK_LOCK|NFSLCK_UNLOCK|NFSLCK_TEST))||
		 ((new_stp->ls_flags & NFSLCK_CHECK) &&
		  (new_lop->lo_flags & NFSLCK_READ))) &&
		  clp != tstp->ls_clp &&
		 (tstp->ls_flags & NFSLCK_DELEGWRITE)) ||
		 ((new_stp->ls_flags & NFSLCK_CHECK) &&
		   (new_lop->lo_flags & NFSLCK_WRITE) &&
		  (clp != tstp->ls_clp ||
		   (tstp->ls_flags & NFSLCK_DELEGREAD)))) {
		ret = 0;
		if (filestruct_locked != 0) {
			/* Roll back local locks. */
			NFSUNLOCKSTATE();
			if (vnode_unlocked == 0) {
				ASSERT_VOP_ELOCKED(vp, "nfsrv_lockctrl4");
				NFSVOPUNLOCK(vp, 0);
			}
			nfsrv_locallock_rollback(vp, lfp, p);
			NFSLOCKSTATE();
			nfsrv_unlocklf(lfp);
			NFSUNLOCKSTATE();
			NFSVOPLOCK(vp, LK_EXCLUSIVE | LK_RETRY);
			vnode_unlocked = 0;
			if ((vp->v_iflag & VI_DOOMED) != 0)
				ret = NFSERR_SERVERFAULT;
			NFSLOCKSTATE();
		}
		if (ret == 0)
			ret = nfsrv_delegconflict(tstp, &haslock, p, vp);
		if (ret) {
		    /*
		     * nfsrv_delegconflict unlocks state when it
		     * returns non-zero, which it always does.
		     */
		    if (other_lop) {
			free(other_lop, M_NFSDLOCK);
			other_lop = NULL;
		    }
		    if (ret == -1) {
			lckstp = NULL;
			goto tryagain;
		    }
		    error = ret;
		    goto out;
		}
		/* Never gets here. */
	    }
	    tstp = nstp;
	}

	/*
	 * Handle the unlock case by calling nfsrv_updatelock().
	 * (Should I have done some access checking above for unlock? For now,
	 *  just let it happen.)
	 */
	if (new_stp->ls_flags & NFSLCK_UNLOCK) {
		first = new_lop->lo_first;
		end = new_lop->lo_end;
		nfsrv_updatelock(stp, new_lopp, &other_lop, lfp);
		stateidp->seqid = ++(stp->ls_stateid.seqid);
		if ((nd->nd_flag & ND_NFSV41) != 0 && stateidp->seqid == 0)
			stateidp->seqid = stp->ls_stateid.seqid = 1;
		stateidp->other[0] = stp->ls_stateid.other[0];
		stateidp->other[1] = stp->ls_stateid.other[1];
		stateidp->other[2] = stp->ls_stateid.other[2];
		if (filestruct_locked != 0) {
			NFSUNLOCKSTATE();
			if (vnode_unlocked == 0) {
				ASSERT_VOP_ELOCKED(vp, "nfsrv_lockctrl5");
				vnode_unlocked = 1;
				NFSVOPUNLOCK(vp, 0);
			}
			/* Update the local locks. */
			nfsrv_localunlock(vp, lfp, first, end, p);
			NFSLOCKSTATE();
			nfsrv_unlocklf(lfp);
		}
		NFSUNLOCKSTATE();
		goto out;
	}

	/*
	 * Search for a conflicting lock. A lock conflicts if:
	 * - the lock range overlaps and
	 * - at least one lock is a write lock and
	 * - it is not owned by the same lock owner
	 */
	if (!delegation) {
	  LIST_FOREACH(lop, &lfp->lf_lock, lo_lckfile) {
	    if (new_lop->lo_end > lop->lo_first &&
		new_lop->lo_first < lop->lo_end &&
		(new_lop->lo_flags == NFSLCK_WRITE ||
		 lop->lo_flags == NFSLCK_WRITE) &&
		lckstp != lop->lo_stp &&
		(clp != lop->lo_stp->ls_clp ||
		 lckstp->ls_ownerlen != lop->lo_stp->ls_ownerlen ||
		 NFSBCMP(lckstp->ls_owner, lop->lo_stp->ls_owner,
		    lckstp->ls_ownerlen))) {
		if (other_lop) {
		    free(other_lop, M_NFSDLOCK);
		    other_lop = NULL;
		}
		if (vnode_unlocked != 0)
		    ret = nfsrv_clientconflict(lop->lo_stp->ls_clp, &haslock,
			NULL, p);
		else
		    ret = nfsrv_clientconflict(lop->lo_stp->ls_clp, &haslock,
			vp, p);
		if (ret == 1) {
		    if (filestruct_locked != 0) {
			if (vnode_unlocked == 0) {
				ASSERT_VOP_ELOCKED(vp, "nfsrv_lockctrl6");
				NFSVOPUNLOCK(vp, 0);
			}
			/* Roll back local locks. */
			nfsrv_locallock_rollback(vp, lfp, p);
			NFSLOCKSTATE();
			nfsrv_unlocklf(lfp);
			NFSUNLOCKSTATE();
			NFSVOPLOCK(vp, LK_EXCLUSIVE | LK_RETRY);
			vnode_unlocked = 0;
			if ((vp->v_iflag & VI_DOOMED) != 0) {
				error = NFSERR_SERVERFAULT;
				goto out;
			}
		    }
		    /*
		     * nfsrv_clientconflict() unlocks state when it
		     * returns non-zero.
		     */
		    lckstp = NULL;
		    goto tryagain;
		}
		/*
		 * Found a conflicting lock, so record the conflict and
		 * return the error.
		 */
		if (cfp != NULL && ret == 0) {
		    cfp->cl_clientid.lval[0]=lop->lo_stp->ls_stateid.other[0];
		    cfp->cl_clientid.lval[1]=lop->lo_stp->ls_stateid.other[1];
		    cfp->cl_first = lop->lo_first;
		    cfp->cl_end = lop->lo_end;
		    cfp->cl_flags = lop->lo_flags;
		    cfp->cl_ownerlen = lop->lo_stp->ls_ownerlen;
		    NFSBCOPY(lop->lo_stp->ls_owner, cfp->cl_owner,
			cfp->cl_ownerlen);
		}
		if (ret == 2)
		    error = NFSERR_PERM;
		else if (new_stp->ls_flags & NFSLCK_RECLAIM)
		    error = NFSERR_RECLAIMCONFLICT;
		else if (new_stp->ls_flags & NFSLCK_CHECK)
		    error = NFSERR_LOCKED;
		else
		    error = NFSERR_DENIED;
		if (filestruct_locked != 0 && ret == 0) {
			/* Roll back local locks. */
			NFSUNLOCKSTATE();
			if (vnode_unlocked == 0) {
				ASSERT_VOP_ELOCKED(vp, "nfsrv_lockctrl7");
				vnode_unlocked = 1;
				NFSVOPUNLOCK(vp, 0);
			}
			nfsrv_locallock_rollback(vp, lfp, p);
			NFSLOCKSTATE();
			nfsrv_unlocklf(lfp);
		}
		if (ret == 0)
			NFSUNLOCKSTATE();
		goto out;
	    }
	  }
	}

	/*
	 * We only get here if there was no lock that conflicted.
	 */
	if (new_stp->ls_flags & (NFSLCK_TEST | NFSLCK_CHECK)) {
		NFSUNLOCKSTATE();
		goto out;
	}

	/*
	 * We only get here when we are creating or modifying a lock.
	 * There are two variants:
	 * - exist_lock_owner where lock_owner exists
	 * - open_to_lock_owner with new lock_owner
	 */
	first = new_lop->lo_first;
	end = new_lop->lo_end;
	lock_flags = new_lop->lo_flags;
	if (!(new_stp->ls_flags & NFSLCK_OPENTOLOCK)) {
		nfsrv_updatelock(lckstp, new_lopp, &other_lop, lfp);
		stateidp->seqid = ++(lckstp->ls_stateid.seqid);
		if ((nd->nd_flag & ND_NFSV41) != 0 && stateidp->seqid == 0)
			stateidp->seqid = lckstp->ls_stateid.seqid = 1;
		stateidp->other[0] = lckstp->ls_stateid.other[0];
		stateidp->other[1] = lckstp->ls_stateid.other[1];
		stateidp->other[2] = lckstp->ls_stateid.other[2];
	} else {
		/*
		 * The new open_to_lock_owner case.
		 * Link the new nfsstate into the lists.
		 */
		new_stp->ls_seq = new_stp->ls_opentolockseq;
		nfsrvd_refcache(new_stp->ls_op);
		stateidp->seqid = new_stp->ls_stateid.seqid = 1;
		stateidp->other[0] = new_stp->ls_stateid.other[0] =
		    clp->lc_clientid.lval[0];
		stateidp->other[1] = new_stp->ls_stateid.other[1] =
		    clp->lc_clientid.lval[1];
		stateidp->other[2] = new_stp->ls_stateid.other[2] =
		    nfsrv_nextstateindex(clp);
		new_stp->ls_clp = clp;
		LIST_INIT(&new_stp->ls_lock);
		new_stp->ls_openstp = stp;
		new_stp->ls_lfp = lfp;
		nfsrv_insertlock(new_lop, (struct nfslock *)new_stp, new_stp,
		    lfp);
		LIST_INSERT_HEAD(NFSSTATEHASH(clp, new_stp->ls_stateid),
		    new_stp, ls_hash);
		LIST_INSERT_HEAD(&stp->ls_open, new_stp, ls_list);
		*new_lopp = NULL;
		*new_stpp = NULL;
		nfsstatsv1.srvlockowners++;
		nfsrv_openpluslock++;
	}
	if (filestruct_locked != 0) {
		NFSUNLOCKSTATE();
		nfsrv_locallock_commit(lfp, lock_flags, first, end);
		NFSLOCKSTATE();
		nfsrv_unlocklf(lfp);
	}
	NFSUNLOCKSTATE();

out:
	if (haslock) {
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();
	}
	if (vnode_unlocked != 0) {
		NFSVOPLOCK(vp, LK_EXCLUSIVE | LK_RETRY);
		if (error == 0 && (vp->v_iflag & VI_DOOMED) != 0)
			error = NFSERR_SERVERFAULT;
	}
	if (other_lop)
		free(other_lop, M_NFSDLOCK);
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Check for state errors for Open.
 * repstat is passed back out as an error if more critical errors
 * are not detected.
 */
APPLESTATIC int
nfsrv_opencheck(nfsquad_t clientid, nfsv4stateid_t *stateidp,
    struct nfsstate *new_stp, vnode_t vp, struct nfsrv_descript *nd,
    NFSPROC_T *p, int repstat)
{
	struct nfsstate *stp, *nstp;
	struct nfsclient *clp;
	struct nfsstate *ownerstp;
	struct nfslockfile *lfp, *new_lfp;
	int error = 0, haslock = 0, ret, readonly = 0, getfhret = 0;

	if ((new_stp->ls_flags & NFSLCK_SHAREBITS) == NFSLCK_READACCESS)
		readonly = 1;
	/*
	 * Check for restart conditions (client and server).
	 */
	error = nfsrv_checkrestart(clientid, new_stp->ls_flags,
		&new_stp->ls_stateid, 0);
	if (error)
		goto out;

	/*
	 * Check for state resource limit exceeded.
	 * Technically this should be SMP protected, but the worst
	 * case error is "out by one or two" on the count when it
	 * returns NFSERR_RESOURCE and the limit is just a rather
	 * arbitrary high water mark, so no harm is done.
	 */
	if (nfsrv_openpluslock > nfsrv_v4statelimit) {
		error = NFSERR_RESOURCE;
		goto out;
	}

tryagain:
	new_lfp = malloc(sizeof (struct nfslockfile),
	    M_NFSDLOCKFILE, M_WAITOK);
	if (vp)
		getfhret = nfsrv_getlockfh(vp, new_stp->ls_flags, new_lfp,
		    NULL, p);
	NFSLOCKSTATE();
	/*
	 * Get the nfsclient structure.
	 */
	error = nfsrv_getclient(clientid, CLOPS_RENEW, &clp, NULL,
	    (nfsquad_t)((u_quad_t)0), 0, nd, p);

	/*
	 * Look up the open owner. See if it needs confirmation and
	 * check the seq#, as required.
	 */
	if (!error)
		nfsrv_getowner(&clp->lc_open, new_stp, &ownerstp);

	if (!error && ownerstp) {
		error = nfsrv_checkseqid(nd, new_stp->ls_seq, ownerstp,
		    new_stp->ls_op);
		/*
		 * If the OpenOwner hasn't been confirmed, assume the
		 * old one was a replay and this one is ok.
		 * See: RFC3530 Sec. 14.2.18.
		 */
		if (error == NFSERR_BADSEQID &&
		    (ownerstp->ls_flags & NFSLCK_NEEDSCONFIRM))
			error = 0;
	}

	/*
	 * Check for grace.
	 */
	if (!error)
		error = nfsrv_checkgrace(nd, clp, new_stp->ls_flags);
	if ((new_stp->ls_flags & NFSLCK_RECLAIM) && !error &&
		nfsrv_checkstable(clp))
		error = NFSERR_NOGRACE;

	/*
	 * If none of the above errors occurred, let repstat be
	 * returned.
	 */
	if (repstat && !error)
		error = repstat;
	if (error) {
		NFSUNLOCKSTATE();
		if (haslock) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		free(new_lfp, M_NFSDLOCKFILE);
		goto out;
	}

	/*
	 * If vp == NULL, the file doesn't exist yet, so return ok.
	 * (This always happens on the first pass, so haslock must be 0.)
	 */
	if (vp == NULL) {
		NFSUNLOCKSTATE();
		free(new_lfp, M_NFSDLOCKFILE);
		goto out;
	}

	/*
	 * Get the structure for the underlying file.
	 */
	if (getfhret)
		error = getfhret;
	else
		error = nfsrv_getlockfile(new_stp->ls_flags, &new_lfp, &lfp,
		    NULL, 0);
	if (new_lfp)
		free(new_lfp, M_NFSDLOCKFILE);
	if (error) {
		NFSUNLOCKSTATE();
		if (haslock) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		goto out;
	}

	/*
	 * Search for a conflicting open/share.
	 */
	if (new_stp->ls_flags & NFSLCK_DELEGCUR) {
	    /*
	     * For Delegate_Cur, search for the matching Delegation,
	     * which indicates no conflict.
	     * An old delegation should have been recovered by the
	     * client doing a Claim_DELEGATE_Prev, so I won't let
	     * it match and return NFSERR_EXPIRED. Should I let it
	     * match?
	     */
	    LIST_FOREACH(stp, &lfp->lf_deleg, ls_file) {
		if (!(stp->ls_flags & NFSLCK_OLDDELEG) &&
		    (((nd->nd_flag & ND_NFSV41) != 0 &&
		    stateidp->seqid == 0) ||
		    stateidp->seqid == stp->ls_stateid.seqid) &&
		    !NFSBCMP(stateidp->other, stp->ls_stateid.other,
			  NFSX_STATEIDOTHER))
			break;
	    }
	    if (stp == LIST_END(&lfp->lf_deleg) ||
		((new_stp->ls_flags & NFSLCK_WRITEACCESS) &&
		 (stp->ls_flags & NFSLCK_DELEGREAD))) {
		NFSUNLOCKSTATE();
		if (haslock) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		error = NFSERR_EXPIRED;
		goto out;
	    }
	}

	/*
	 * Check for access/deny bit conflicts. I check for the same
	 * owner as well, in case the client didn't bother.
	 */
	LIST_FOREACH(stp, &lfp->lf_open, ls_file) {
		if (!(new_stp->ls_flags & NFSLCK_DELEGCUR) &&
		    (((new_stp->ls_flags & NFSLCK_ACCESSBITS) &
		      ((stp->ls_flags>>NFSLCK_SHIFT) & NFSLCK_ACCESSBITS))||
		     ((stp->ls_flags & NFSLCK_ACCESSBITS) &
		      ((new_stp->ls_flags>>NFSLCK_SHIFT)&NFSLCK_ACCESSBITS)))){
			ret = nfsrv_clientconflict(stp->ls_clp,&haslock,vp,p);
			if (ret == 1) {
				/*
				 * nfsrv_clientconflict() unlocks
				 * state when it returns non-zero.
				 */
				goto tryagain;
			}
			if (ret == 2)
				error = NFSERR_PERM;
			else if (new_stp->ls_flags & NFSLCK_RECLAIM)
				error = NFSERR_RECLAIMCONFLICT;
			else
				error = NFSERR_SHAREDENIED;
			if (ret == 0)
				NFSUNLOCKSTATE();
			if (haslock) {
				NFSLOCKV4ROOTMUTEX();
				nfsv4_unlock(&nfsv4rootfs_lock, 1);
				NFSUNLOCKV4ROOTMUTEX();
			}
			goto out;
		}
	}

	/*
	 * Check for a conflicting delegation. If one is found, call
	 * nfsrv_delegconflict() to handle it. If the v4root lock hasn't
	 * been set yet, it will get the lock. Otherwise, it will recall
	 * the delegation. Then, we try try again...
	 * (If NFSLCK_DELEGCUR is set, it has a delegation, so there
	 *  isn't a conflict.)
	 * I currently believe the conflict algorithm to be:
	 * For Open with Read Access and Deny None
	 * - there is a conflict iff a different client has a write delegation
	 * For Open with other Write Access or any Deny except None
	 * - there is a conflict if a different client has any delegation
	 * - there is a conflict if the same client has a read delegation
	 *   (The current consensus is that this last case should be
	 *    considered a conflict since the client with a read delegation
	 *    could have done an Open with ReadAccess and WriteDeny
	 *    locally and then not have checked for the WriteDeny.)
	 * Don't check for a Reclaim, since that will be dealt with
	 * by nfsrv_openctrl().
	 */
	if (!(new_stp->ls_flags &
		(NFSLCK_DELEGPREV | NFSLCK_DELEGCUR | NFSLCK_RECLAIM))) {
	    stp = LIST_FIRST(&lfp->lf_deleg);
	    while (stp != LIST_END(&lfp->lf_deleg)) {
		nstp = LIST_NEXT(stp, ls_file);
		if ((readonly && stp->ls_clp != clp &&
		       (stp->ls_flags & NFSLCK_DELEGWRITE)) ||
		    (!readonly && (stp->ls_clp != clp ||
		         (stp->ls_flags & NFSLCK_DELEGREAD)))) {
			ret = nfsrv_delegconflict(stp, &haslock, p, vp);
			if (ret) {
			    /*
			     * nfsrv_delegconflict() unlocks state
			     * when it returns non-zero.
			     */
			    if (ret == -1)
				goto tryagain;
			    error = ret;
			    goto out;
			}
		}
		stp = nstp;
	    }
	}
	NFSUNLOCKSTATE();
	if (haslock) {
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();
	}

out:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Open control function to create/update open state for an open.
 */
APPLESTATIC int
nfsrv_openctrl(struct nfsrv_descript *nd, vnode_t vp,
    struct nfsstate **new_stpp, nfsquad_t clientid, nfsv4stateid_t *stateidp,
    nfsv4stateid_t *delegstateidp, u_int32_t *rflagsp, struct nfsexstuff *exp,
    NFSPROC_T *p, u_quad_t filerev)
{
	struct nfsstate *new_stp = *new_stpp;
	struct nfsstate *stp, *nstp;
	struct nfsstate *openstp = NULL, *new_open, *ownerstp, *new_deleg;
	struct nfslockfile *lfp, *new_lfp;
	struct nfsclient *clp;
	int error = 0, haslock = 0, ret, delegate = 1, writedeleg = 1;
	int readonly = 0, cbret = 1, getfhret = 0;
	int gotstate = 0, len = 0;
	u_char *clidp = NULL;

	if ((new_stp->ls_flags & NFSLCK_SHAREBITS) == NFSLCK_READACCESS)
		readonly = 1;
	/*
	 * Check for restart conditions (client and server).
	 * (Paranoia, should have been detected by nfsrv_opencheck().)
	 * If an error does show up, return NFSERR_EXPIRED, since the
	 * the seqid# has already been incremented.
	 */
	error = nfsrv_checkrestart(clientid, new_stp->ls_flags,
	    &new_stp->ls_stateid, 0);
	if (error) {
		printf("Nfsd: openctrl unexpected restart err=%d\n",
		    error);
		error = NFSERR_EXPIRED;
		goto out;
	}

	clidp = malloc(NFSV4_OPAQUELIMIT, M_TEMP, M_WAITOK);
tryagain:
	new_lfp = malloc(sizeof (struct nfslockfile),
	    M_NFSDLOCKFILE, M_WAITOK);
	new_open = malloc(sizeof (struct nfsstate),
	    M_NFSDSTATE, M_WAITOK);
	new_deleg = malloc(sizeof (struct nfsstate),
	    M_NFSDSTATE, M_WAITOK);
	getfhret = nfsrv_getlockfh(vp, new_stp->ls_flags, new_lfp,
	    NULL, p);
	NFSLOCKSTATE();
	/*
	 * Get the client structure. Since the linked lists could be changed
	 * by other nfsd processes if this process does a tsleep(), one of
	 * two things must be done.
	 * 1 - don't tsleep()
	 * or
	 * 2 - get the nfsv4_lock() { indicated by haslock == 1 }
	 *     before using the lists, since this lock stops the other
	 *     nfsd. This should only be used for rare cases, since it
	 *     essentially single threads the nfsd.
	 *     At this time, it is only done for cases where the stable
	 *     storage file must be written prior to completion of state
	 *     expiration.
	 */
	error = nfsrv_getclient(clientid, CLOPS_RENEW, &clp, NULL,
	    (nfsquad_t)((u_quad_t)0), 0, nd, p);
	if (!error && (clp->lc_flags & LCL_NEEDSCBNULL) &&
	    clp->lc_program) {
		/*
		 * This happens on the first open for a client
		 * that supports callbacks.
		 */
		NFSUNLOCKSTATE();
		/*
		 * Although nfsrv_docallback() will sleep, clp won't
		 * go away, since they are only removed when the
		 * nfsv4_lock() has blocked the nfsd threads. The
		 * fields in clp can change, but having multiple
		 * threads do this Null callback RPC should be
		 * harmless.
		 */
		cbret = nfsrv_docallback(clp, NFSV4PROC_CBNULL,
		    NULL, 0, NULL, NULL, NULL, 0, p);
		NFSLOCKSTATE();
		clp->lc_flags &= ~LCL_NEEDSCBNULL;
		if (!cbret)
			clp->lc_flags |= LCL_CALLBACKSON;
	}

	/*
	 * Look up the open owner. See if it needs confirmation and
	 * check the seq#, as required.
	 */
	if (!error)
		nfsrv_getowner(&clp->lc_open, new_stp, &ownerstp);

	if (error) {
		NFSUNLOCKSTATE();
		printf("Nfsd: openctrl unexpected state err=%d\n",
			error);
		free(new_lfp, M_NFSDLOCKFILE);
		free(new_open, M_NFSDSTATE);
		free(new_deleg, M_NFSDSTATE);
		if (haslock) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		error = NFSERR_EXPIRED;
		goto out;
	}

	if (new_stp->ls_flags & NFSLCK_RECLAIM)
		nfsrv_markstable(clp);

	/*
	 * Get the structure for the underlying file.
	 */
	if (getfhret)
		error = getfhret;
	else
		error = nfsrv_getlockfile(new_stp->ls_flags, &new_lfp, &lfp,
		    NULL, 0);
	if (new_lfp)
		free(new_lfp, M_NFSDLOCKFILE);
	if (error) {
		NFSUNLOCKSTATE();
		printf("Nfsd openctrl unexpected getlockfile err=%d\n",
		    error);
		free(new_open, M_NFSDSTATE);
		free(new_deleg, M_NFSDSTATE);
		if (haslock) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		goto out;
	}

	/*
	 * Search for a conflicting open/share.
	 */
	if (new_stp->ls_flags & NFSLCK_DELEGCUR) {
	    /*
	     * For Delegate_Cur, search for the matching Delegation,
	     * which indicates no conflict.
	     * An old delegation should have been recovered by the
	     * client doing a Claim_DELEGATE_Prev, so I won't let
	     * it match and return NFSERR_EXPIRED. Should I let it
	     * match?
	     */
	    LIST_FOREACH(stp, &lfp->lf_deleg, ls_file) {
		if (!(stp->ls_flags & NFSLCK_OLDDELEG) &&
		    (((nd->nd_flag & ND_NFSV41) != 0 &&
		    stateidp->seqid == 0) ||
		    stateidp->seqid == stp->ls_stateid.seqid) &&
		    !NFSBCMP(stateidp->other, stp->ls_stateid.other,
			NFSX_STATEIDOTHER))
			break;
	    }
	    if (stp == LIST_END(&lfp->lf_deleg) ||
		((new_stp->ls_flags & NFSLCK_WRITEACCESS) &&
		 (stp->ls_flags & NFSLCK_DELEGREAD))) {
		NFSUNLOCKSTATE();
		printf("Nfsd openctrl unexpected expiry\n");
		free(new_open, M_NFSDSTATE);
		free(new_deleg, M_NFSDSTATE);
		if (haslock) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		error = NFSERR_EXPIRED;
		goto out;
	    }

	    /*
	     * Don't issue a Delegation, since one already exists and
	     * delay delegation timeout, as required.
	     */
	    delegate = 0;
	    nfsrv_delaydelegtimeout(stp);
	}

	/*
	 * Check for access/deny bit conflicts. I also check for the
	 * same owner, since the client might not have bothered to check.
	 * Also, note an open for the same file and owner, if found,
	 * which is all we do here for Delegate_Cur, since conflict
	 * checking is already done.
	 */
	LIST_FOREACH(stp, &lfp->lf_open, ls_file) {
		if (ownerstp && stp->ls_openowner == ownerstp)
			openstp = stp;
		if (!(new_stp->ls_flags & NFSLCK_DELEGCUR)) {
		    /*
		     * If another client has the file open, the only
		     * delegation that can be issued is a Read delegation
		     * and only if it is a Read open with Deny none.
		     */
		    if (clp != stp->ls_clp) {
			if ((stp->ls_flags & NFSLCK_SHAREBITS) ==
			    NFSLCK_READACCESS)
			    writedeleg = 0;
			else
			    delegate = 0;
		    }
		    if(((new_stp->ls_flags & NFSLCK_ACCESSBITS) &
		        ((stp->ls_flags>>NFSLCK_SHIFT) & NFSLCK_ACCESSBITS))||
		       ((stp->ls_flags & NFSLCK_ACCESSBITS) &
		        ((new_stp->ls_flags>>NFSLCK_SHIFT)&NFSLCK_ACCESSBITS))){
			ret = nfsrv_clientconflict(stp->ls_clp,&haslock,vp,p);
			if (ret == 1) {
				/*
				 * nfsrv_clientconflict() unlocks state
				 * when it returns non-zero.
				 */
				free(new_open, M_NFSDSTATE);
				free(new_deleg, M_NFSDSTATE);
				openstp = NULL;
				goto tryagain;
			}
			if (ret == 2)
				error = NFSERR_PERM;
			else if (new_stp->ls_flags & NFSLCK_RECLAIM)
				error = NFSERR_RECLAIMCONFLICT;
			else
				error = NFSERR_SHAREDENIED;
			if (ret == 0)
				NFSUNLOCKSTATE();
			if (haslock) {
				NFSLOCKV4ROOTMUTEX();
				nfsv4_unlock(&nfsv4rootfs_lock, 1);
				NFSUNLOCKV4ROOTMUTEX();
			}
			free(new_open, M_NFSDSTATE);
			free(new_deleg, M_NFSDSTATE);
			printf("nfsd openctrl unexpected client cnfl\n");
			goto out;
		    }
		}
	}

	/*
	 * Check for a conflicting delegation. If one is found, call
	 * nfsrv_delegconflict() to handle it. If the v4root lock hasn't
	 * been set yet, it will get the lock. Otherwise, it will recall
	 * the delegation. Then, we try try again...
	 * (If NFSLCK_DELEGCUR is set, it has a delegation, so there
	 *  isn't a conflict.)
	 * I currently believe the conflict algorithm to be:
	 * For Open with Read Access and Deny None
	 * - there is a conflict iff a different client has a write delegation
	 * For Open with other Write Access or any Deny except None
	 * - there is a conflict if a different client has any delegation
	 * - there is a conflict if the same client has a read delegation
	 *   (The current consensus is that this last case should be
	 *    considered a conflict since the client with a read delegation
	 *    could have done an Open with ReadAccess and WriteDeny
	 *    locally and then not have checked for the WriteDeny.)
	 */
	if (!(new_stp->ls_flags & (NFSLCK_DELEGPREV | NFSLCK_DELEGCUR))) {
	    stp = LIST_FIRST(&lfp->lf_deleg);
	    while (stp != LIST_END(&lfp->lf_deleg)) {
		nstp = LIST_NEXT(stp, ls_file);
		if (stp->ls_clp != clp && (stp->ls_flags & NFSLCK_DELEGREAD))
			writedeleg = 0;
		else
			delegate = 0;
		if ((readonly && stp->ls_clp != clp &&
		       (stp->ls_flags & NFSLCK_DELEGWRITE)) ||
		    (!readonly && (stp->ls_clp != clp ||
		         (stp->ls_flags & NFSLCK_DELEGREAD)))) {
		    if (new_stp->ls_flags & NFSLCK_RECLAIM) {
			delegate = 2;
		    } else {
			ret = nfsrv_delegconflict(stp, &haslock, p, vp);
			if (ret) {
			    /*
			     * nfsrv_delegconflict() unlocks state
			     * when it returns non-zero.
			     */
			    printf("Nfsd openctrl unexpected deleg cnfl\n");
			    free(new_open, M_NFSDSTATE);
			    free(new_deleg, M_NFSDSTATE);
			    if (ret == -1) {
				openstp = NULL;
				goto tryagain;
			    }
			    error = ret;
			    goto out;
			}
		    }
		}
		stp = nstp;
	    }
	}

	/*
	 * We only get here if there was no open that conflicted.
	 * If an open for the owner exists, or in the access/deny bits.
	 * Otherwise it is a new open. If the open_owner hasn't been
	 * confirmed, replace the open with the new one needing confirmation,
	 * otherwise add the open.
	 */
	if (new_stp->ls_flags & NFSLCK_DELEGPREV) {
	    /*
	     * Handle NFSLCK_DELEGPREV by searching the old delegations for
	     * a match. If found, just move the old delegation to the current
	     * delegation list and issue open. If not found, return
	     * NFSERR_EXPIRED.
	     */
	    LIST_FOREACH(stp, &clp->lc_olddeleg, ls_list) {
		if (stp->ls_lfp == lfp) {
		    /* Found it */
		    if (stp->ls_clp != clp)
			panic("olddeleg clp");
		    LIST_REMOVE(stp, ls_list);
		    LIST_REMOVE(stp, ls_hash);
		    stp->ls_flags &= ~NFSLCK_OLDDELEG;
		    stp->ls_stateid.seqid = delegstateidp->seqid = 1;
		    stp->ls_stateid.other[0] = delegstateidp->other[0] =
			clp->lc_clientid.lval[0];
		    stp->ls_stateid.other[1] = delegstateidp->other[1] =
			clp->lc_clientid.lval[1];
		    stp->ls_stateid.other[2] = delegstateidp->other[2] =
			nfsrv_nextstateindex(clp);
		    stp->ls_compref = nd->nd_compref;
		    LIST_INSERT_HEAD(&clp->lc_deleg, stp, ls_list);
		    LIST_INSERT_HEAD(NFSSTATEHASH(clp,
			stp->ls_stateid), stp, ls_hash);
		    if (stp->ls_flags & NFSLCK_DELEGWRITE)
			*rflagsp |= NFSV4OPEN_WRITEDELEGATE;
		    else
			*rflagsp |= NFSV4OPEN_READDELEGATE;
		    clp->lc_delegtime = NFSD_MONOSEC +
			nfsrv_lease + NFSRV_LEASEDELTA;

		    /*
		     * Now, do the associated open.
		     */
		    new_open->ls_stateid.seqid = 1;
		    new_open->ls_stateid.other[0] = clp->lc_clientid.lval[0];
		    new_open->ls_stateid.other[1] = clp->lc_clientid.lval[1];
		    new_open->ls_stateid.other[2] = nfsrv_nextstateindex(clp);
		    new_open->ls_flags = (new_stp->ls_flags&NFSLCK_DENYBITS)|
			NFSLCK_OPEN;
		    if (stp->ls_flags & NFSLCK_DELEGWRITE)
			new_open->ls_flags |= (NFSLCK_READACCESS |
			    NFSLCK_WRITEACCESS);
		    else
			new_open->ls_flags |= NFSLCK_READACCESS;
		    new_open->ls_uid = new_stp->ls_uid;
		    new_open->ls_lfp = lfp;
		    new_open->ls_clp = clp;
		    LIST_INIT(&new_open->ls_open);
		    LIST_INSERT_HEAD(&lfp->lf_open, new_open, ls_file);
		    LIST_INSERT_HEAD(NFSSTATEHASH(clp, new_open->ls_stateid),
			new_open, ls_hash);
		    /*
		     * and handle the open owner
		     */
		    if (ownerstp) {
			new_open->ls_openowner = ownerstp;
			LIST_INSERT_HEAD(&ownerstp->ls_open,new_open,ls_list);
		    } else {
			new_open->ls_openowner = new_stp;
			new_stp->ls_flags = 0;
			nfsrvd_refcache(new_stp->ls_op);
			new_stp->ls_noopens = 0;
			LIST_INIT(&new_stp->ls_open);
			LIST_INSERT_HEAD(&new_stp->ls_open, new_open, ls_list);
			LIST_INSERT_HEAD(&clp->lc_open, new_stp, ls_list);
			*new_stpp = NULL;
			nfsstatsv1.srvopenowners++;
			nfsrv_openpluslock++;
		    }
		    openstp = new_open;
		    new_open = NULL;
		    nfsstatsv1.srvopens++;
		    nfsrv_openpluslock++;
		    break;
		}
	    }
	    if (stp == LIST_END(&clp->lc_olddeleg))
		error = NFSERR_EXPIRED;
	} else if (new_stp->ls_flags & (NFSLCK_DELEGREAD | NFSLCK_DELEGWRITE)) {
	    /*
	     * Scan to see that no delegation for this client and file
	     * doesn't already exist.
	     * There also shouldn't yet be an Open for this file and
	     * openowner.
	     */
	    LIST_FOREACH(stp, &lfp->lf_deleg, ls_file) {
		if (stp->ls_clp == clp)
		    break;
	    }
	    if (stp == LIST_END(&lfp->lf_deleg) && openstp == NULL) {
		/*
		 * This is the Claim_Previous case with a delegation
		 * type != Delegate_None.
		 */
		/*
		 * First, add the delegation. (Although we must issue the
		 * delegation, we can also ask for an immediate return.)
		 */
		new_deleg->ls_stateid.seqid = delegstateidp->seqid = 1;
		new_deleg->ls_stateid.other[0] = delegstateidp->other[0] =
		    clp->lc_clientid.lval[0];
		new_deleg->ls_stateid.other[1] = delegstateidp->other[1] =
		    clp->lc_clientid.lval[1];
		new_deleg->ls_stateid.other[2] = delegstateidp->other[2] =
		    nfsrv_nextstateindex(clp);
		if (new_stp->ls_flags & NFSLCK_DELEGWRITE) {
		    new_deleg->ls_flags = (NFSLCK_DELEGWRITE |
			NFSLCK_READACCESS | NFSLCK_WRITEACCESS);
		    *rflagsp |= NFSV4OPEN_WRITEDELEGATE;
		    nfsrv_writedelegcnt++;
		} else {
		    new_deleg->ls_flags = (NFSLCK_DELEGREAD |
			NFSLCK_READACCESS);
		    *rflagsp |= NFSV4OPEN_READDELEGATE;
		}
		new_deleg->ls_uid = new_stp->ls_uid;
		new_deleg->ls_lfp = lfp;
		new_deleg->ls_clp = clp;
		new_deleg->ls_filerev = filerev;
		new_deleg->ls_compref = nd->nd_compref;
		LIST_INSERT_HEAD(&lfp->lf_deleg, new_deleg, ls_file);
		LIST_INSERT_HEAD(NFSSTATEHASH(clp,
		    new_deleg->ls_stateid), new_deleg, ls_hash);
		LIST_INSERT_HEAD(&clp->lc_deleg, new_deleg, ls_list);
		new_deleg = NULL;
		if (delegate == 2 || nfsrv_issuedelegs == 0 ||
		    (clp->lc_flags & (LCL_CALLBACKSON | LCL_CBDOWN)) !=
		     LCL_CALLBACKSON ||
		    NFSRV_V4DELEGLIMIT(nfsrv_delegatecnt) ||
		    !NFSVNO_DELEGOK(vp))
		    *rflagsp |= NFSV4OPEN_RECALL;
		nfsstatsv1.srvdelegates++;
		nfsrv_openpluslock++;
		nfsrv_delegatecnt++;

		/*
		 * Now, do the associated open.
		 */
		new_open->ls_stateid.seqid = 1;
		new_open->ls_stateid.other[0] = clp->lc_clientid.lval[0];
		new_open->ls_stateid.other[1] = clp->lc_clientid.lval[1];
		new_open->ls_stateid.other[2] = nfsrv_nextstateindex(clp);
		new_open->ls_flags = (new_stp->ls_flags & NFSLCK_DENYBITS) |
		    NFSLCK_OPEN;
		if (new_stp->ls_flags & NFSLCK_DELEGWRITE)
			new_open->ls_flags |= (NFSLCK_READACCESS |
			    NFSLCK_WRITEACCESS);
		else
			new_open->ls_flags |= NFSLCK_READACCESS;
		new_open->ls_uid = new_stp->ls_uid;
		new_open->ls_lfp = lfp;
		new_open->ls_clp = clp;
		LIST_INIT(&new_open->ls_open);
		LIST_INSERT_HEAD(&lfp->lf_open, new_open, ls_file);
		LIST_INSERT_HEAD(NFSSTATEHASH(clp, new_open->ls_stateid),
		   new_open, ls_hash);
		/*
		 * and handle the open owner
		 */
		if (ownerstp) {
		    new_open->ls_openowner = ownerstp;
		    LIST_INSERT_HEAD(&ownerstp->ls_open, new_open, ls_list);
		} else {
		    new_open->ls_openowner = new_stp;
		    new_stp->ls_flags = 0;
		    nfsrvd_refcache(new_stp->ls_op);
		    new_stp->ls_noopens = 0;
		    LIST_INIT(&new_stp->ls_open);
		    LIST_INSERT_HEAD(&new_stp->ls_open, new_open, ls_list);
		    LIST_INSERT_HEAD(&clp->lc_open, new_stp, ls_list);
		    *new_stpp = NULL;
		    nfsstatsv1.srvopenowners++;
		    nfsrv_openpluslock++;
		}
		openstp = new_open;
		new_open = NULL;
		nfsstatsv1.srvopens++;
		nfsrv_openpluslock++;
	    } else {
		error = NFSERR_RECLAIMCONFLICT;
	    }
	} else if (ownerstp) {
		if (ownerstp->ls_flags & NFSLCK_NEEDSCONFIRM) {
		    /* Replace the open */
		    if (ownerstp->ls_op)
			nfsrvd_derefcache(ownerstp->ls_op);
		    ownerstp->ls_op = new_stp->ls_op;
		    nfsrvd_refcache(ownerstp->ls_op);
		    ownerstp->ls_seq = new_stp->ls_seq;
		    *rflagsp |= NFSV4OPEN_RESULTCONFIRM;
		    stp = LIST_FIRST(&ownerstp->ls_open);
		    stp->ls_flags = (new_stp->ls_flags & NFSLCK_SHAREBITS) |
			NFSLCK_OPEN;
		    stp->ls_stateid.seqid = 1;
		    stp->ls_uid = new_stp->ls_uid;
		    if (lfp != stp->ls_lfp) {
			LIST_REMOVE(stp, ls_file);
			LIST_INSERT_HEAD(&lfp->lf_open, stp, ls_file);
			stp->ls_lfp = lfp;
		    }
		    openstp = stp;
		} else if (openstp) {
		    openstp->ls_flags |= (new_stp->ls_flags & NFSLCK_SHAREBITS);
		    openstp->ls_stateid.seqid++;
		    if ((nd->nd_flag & ND_NFSV41) != 0 &&
			openstp->ls_stateid.seqid == 0)
			openstp->ls_stateid.seqid = 1;

		    /*
		     * This is where we can choose to issue a delegation.
		     */
		    if ((new_stp->ls_flags & NFSLCK_WANTNODELEG) != 0)
			*rflagsp |= NFSV4OPEN_WDNOTWANTED;
		    else if (nfsrv_issuedelegs == 0)
			*rflagsp |= NFSV4OPEN_WDSUPPFTYPE;
		    else if (NFSRV_V4DELEGLIMIT(nfsrv_delegatecnt))
			*rflagsp |= NFSV4OPEN_WDRESOURCE;
		    else if (delegate == 0 || writedeleg == 0 ||
			NFSVNO_EXRDONLY(exp) || (readonly != 0 &&
			nfsrv_writedelegifpos == 0) ||
			!NFSVNO_DELEGOK(vp) ||
			(new_stp->ls_flags & NFSLCK_WANTRDELEG) != 0 ||
			(clp->lc_flags & (LCL_CALLBACKSON | LCL_CBDOWN)) !=
			 LCL_CALLBACKSON)
			*rflagsp |= NFSV4OPEN_WDCONTENTION;
		    else {
			new_deleg->ls_stateid.seqid = delegstateidp->seqid = 1;
			new_deleg->ls_stateid.other[0] = delegstateidp->other[0]
			    = clp->lc_clientid.lval[0];
			new_deleg->ls_stateid.other[1] = delegstateidp->other[1]
			    = clp->lc_clientid.lval[1];
			new_deleg->ls_stateid.other[2] = delegstateidp->other[2]
			    = nfsrv_nextstateindex(clp);
			new_deleg->ls_flags = (NFSLCK_DELEGWRITE |
			    NFSLCK_READACCESS | NFSLCK_WRITEACCESS);
			*rflagsp |= NFSV4OPEN_WRITEDELEGATE;
			new_deleg->ls_uid = new_stp->ls_uid;
			new_deleg->ls_lfp = lfp;
			new_deleg->ls_clp = clp;
			new_deleg->ls_filerev = filerev;
			new_deleg->ls_compref = nd->nd_compref;
			nfsrv_writedelegcnt++;
			LIST_INSERT_HEAD(&lfp->lf_deleg, new_deleg, ls_file);
			LIST_INSERT_HEAD(NFSSTATEHASH(clp,
			    new_deleg->ls_stateid), new_deleg, ls_hash);
			LIST_INSERT_HEAD(&clp->lc_deleg, new_deleg, ls_list);
			new_deleg = NULL;
			nfsstatsv1.srvdelegates++;
			nfsrv_openpluslock++;
			nfsrv_delegatecnt++;
		    }
		} else {
		    new_open->ls_stateid.seqid = 1;
		    new_open->ls_stateid.other[0] = clp->lc_clientid.lval[0];
		    new_open->ls_stateid.other[1] = clp->lc_clientid.lval[1];
		    new_open->ls_stateid.other[2] = nfsrv_nextstateindex(clp);
		    new_open->ls_flags = (new_stp->ls_flags & NFSLCK_SHAREBITS)|
			NFSLCK_OPEN;
		    new_open->ls_uid = new_stp->ls_uid;
		    new_open->ls_openowner = ownerstp;
		    new_open->ls_lfp = lfp;
		    new_open->ls_clp = clp;
		    LIST_INIT(&new_open->ls_open);
		    LIST_INSERT_HEAD(&lfp->lf_open, new_open, ls_file);
		    LIST_INSERT_HEAD(&ownerstp->ls_open, new_open, ls_list);
		    LIST_INSERT_HEAD(NFSSTATEHASH(clp, new_open->ls_stateid),
			new_open, ls_hash);
		    openstp = new_open;
		    new_open = NULL;
		    nfsstatsv1.srvopens++;
		    nfsrv_openpluslock++;

		    /*
		     * This is where we can choose to issue a delegation.
		     */
		    if ((new_stp->ls_flags & NFSLCK_WANTNODELEG) != 0)
			*rflagsp |= NFSV4OPEN_WDNOTWANTED;
		    else if (nfsrv_issuedelegs == 0)
			*rflagsp |= NFSV4OPEN_WDSUPPFTYPE;
		    else if (NFSRV_V4DELEGLIMIT(nfsrv_delegatecnt))
			*rflagsp |= NFSV4OPEN_WDRESOURCE;
		    else if (delegate == 0 || (writedeleg == 0 &&
			readonly == 0) || !NFSVNO_DELEGOK(vp) ||
			(clp->lc_flags & (LCL_CALLBACKSON | LCL_CBDOWN)) !=
			 LCL_CALLBACKSON)
			*rflagsp |= NFSV4OPEN_WDCONTENTION;
		    else {
			new_deleg->ls_stateid.seqid = delegstateidp->seqid = 1;
			new_deleg->ls_stateid.other[0] = delegstateidp->other[0]
			    = clp->lc_clientid.lval[0];
			new_deleg->ls_stateid.other[1] = delegstateidp->other[1]
			    = clp->lc_clientid.lval[1];
			new_deleg->ls_stateid.other[2] = delegstateidp->other[2]
			    = nfsrv_nextstateindex(clp);
			if (writedeleg && !NFSVNO_EXRDONLY(exp) &&
			    (nfsrv_writedelegifpos || !readonly) &&
			    (new_stp->ls_flags & NFSLCK_WANTRDELEG) == 0) {
			    new_deleg->ls_flags = (NFSLCK_DELEGWRITE |
				NFSLCK_READACCESS | NFSLCK_WRITEACCESS);
			    *rflagsp |= NFSV4OPEN_WRITEDELEGATE;
			    nfsrv_writedelegcnt++;
			} else {
			    new_deleg->ls_flags = (NFSLCK_DELEGREAD |
				NFSLCK_READACCESS);
			    *rflagsp |= NFSV4OPEN_READDELEGATE;
			}
			new_deleg->ls_uid = new_stp->ls_uid;
			new_deleg->ls_lfp = lfp;
			new_deleg->ls_clp = clp;
			new_deleg->ls_filerev = filerev;
			new_deleg->ls_compref = nd->nd_compref;
			LIST_INSERT_HEAD(&lfp->lf_deleg, new_deleg, ls_file);
			LIST_INSERT_HEAD(NFSSTATEHASH(clp,
			    new_deleg->ls_stateid), new_deleg, ls_hash);
			LIST_INSERT_HEAD(&clp->lc_deleg, new_deleg, ls_list);
			new_deleg = NULL;
			nfsstatsv1.srvdelegates++;
			nfsrv_openpluslock++;
			nfsrv_delegatecnt++;
		    }
		}
	} else {
		/*
		 * New owner case. Start the open_owner sequence with a
		 * Needs confirmation (unless a reclaim) and hang the
		 * new open off it.
		 */
		new_open->ls_stateid.seqid = 1;
		new_open->ls_stateid.other[0] = clp->lc_clientid.lval[0];
		new_open->ls_stateid.other[1] = clp->lc_clientid.lval[1];
		new_open->ls_stateid.other[2] = nfsrv_nextstateindex(clp);
		new_open->ls_flags = (new_stp->ls_flags & NFSLCK_SHAREBITS) |
		    NFSLCK_OPEN;
		new_open->ls_uid = new_stp->ls_uid;
		LIST_INIT(&new_open->ls_open);
		new_open->ls_openowner = new_stp;
		new_open->ls_lfp = lfp;
		new_open->ls_clp = clp;
		LIST_INSERT_HEAD(&lfp->lf_open, new_open, ls_file);
		if (new_stp->ls_flags & NFSLCK_RECLAIM) {
			new_stp->ls_flags = 0;
		} else if ((nd->nd_flag & ND_NFSV41) != 0) {
			/* NFSv4.1 never needs confirmation. */
			new_stp->ls_flags = 0;

			/*
			 * This is where we can choose to issue a delegation.
			 */
			if (delegate && nfsrv_issuedelegs &&
			    (writedeleg || readonly) &&
			    (clp->lc_flags & (LCL_CALLBACKSON | LCL_CBDOWN)) ==
			     LCL_CALLBACKSON &&
			    !NFSRV_V4DELEGLIMIT(nfsrv_delegatecnt) &&
			    NFSVNO_DELEGOK(vp) &&
			    ((nd->nd_flag & ND_NFSV41) == 0 ||
			     (new_stp->ls_flags & NFSLCK_WANTNODELEG) == 0)) {
				new_deleg->ls_stateid.seqid =
				    delegstateidp->seqid = 1;
				new_deleg->ls_stateid.other[0] =
				    delegstateidp->other[0]
				    = clp->lc_clientid.lval[0];
				new_deleg->ls_stateid.other[1] =
				    delegstateidp->other[1]
				    = clp->lc_clientid.lval[1];
				new_deleg->ls_stateid.other[2] =
				    delegstateidp->other[2]
				    = nfsrv_nextstateindex(clp);
				if (writedeleg && !NFSVNO_EXRDONLY(exp) &&
				    (nfsrv_writedelegifpos || !readonly) &&
				    ((nd->nd_flag & ND_NFSV41) == 0 ||
				     (new_stp->ls_flags & NFSLCK_WANTRDELEG) ==
				     0)) {
					new_deleg->ls_flags =
					    (NFSLCK_DELEGWRITE |
					     NFSLCK_READACCESS |
					     NFSLCK_WRITEACCESS);
					*rflagsp |= NFSV4OPEN_WRITEDELEGATE;
					nfsrv_writedelegcnt++;
				} else {
					new_deleg->ls_flags =
					    (NFSLCK_DELEGREAD |
					     NFSLCK_READACCESS);
					*rflagsp |= NFSV4OPEN_READDELEGATE;
				}
				new_deleg->ls_uid = new_stp->ls_uid;
				new_deleg->ls_lfp = lfp;
				new_deleg->ls_clp = clp;
				new_deleg->ls_filerev = filerev;
				new_deleg->ls_compref = nd->nd_compref;
				LIST_INSERT_HEAD(&lfp->lf_deleg, new_deleg,
				    ls_file);
				LIST_INSERT_HEAD(NFSSTATEHASH(clp,
				    new_deleg->ls_stateid), new_deleg, ls_hash);
				LIST_INSERT_HEAD(&clp->lc_deleg, new_deleg,
				    ls_list);
				new_deleg = NULL;
				nfsstatsv1.srvdelegates++;
				nfsrv_openpluslock++;
				nfsrv_delegatecnt++;
			}
			/*
			 * Since NFSv4.1 never does an OpenConfirm, the first
			 * open state will be acquired here.
			 */
			if (!(clp->lc_flags & LCL_STAMPEDSTABLE)) {
				clp->lc_flags |= LCL_STAMPEDSTABLE;
				len = clp->lc_idlen;
				NFSBCOPY(clp->lc_id, clidp, len);
				gotstate = 1;
			}
		} else {
			*rflagsp |= NFSV4OPEN_RESULTCONFIRM;
			new_stp->ls_flags = NFSLCK_NEEDSCONFIRM;
		}
		nfsrvd_refcache(new_stp->ls_op);
		new_stp->ls_noopens = 0;
		LIST_INIT(&new_stp->ls_open);
		LIST_INSERT_HEAD(&new_stp->ls_open, new_open, ls_list);
		LIST_INSERT_HEAD(&clp->lc_open, new_stp, ls_list);
		LIST_INSERT_HEAD(NFSSTATEHASH(clp, new_open->ls_stateid),
		    new_open, ls_hash);
		openstp = new_open;
		new_open = NULL;
		*new_stpp = NULL;
		nfsstatsv1.srvopens++;
		nfsrv_openpluslock++;
		nfsstatsv1.srvopenowners++;
		nfsrv_openpluslock++;
	}
	if (!error) {
		stateidp->seqid = openstp->ls_stateid.seqid;
		stateidp->other[0] = openstp->ls_stateid.other[0];
		stateidp->other[1] = openstp->ls_stateid.other[1];
		stateidp->other[2] = openstp->ls_stateid.other[2];
	}
	NFSUNLOCKSTATE();
	if (haslock) {
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();
	}
	if (new_open)
		free(new_open, M_NFSDSTATE);
	if (new_deleg)
		free(new_deleg, M_NFSDSTATE);

	/*
	 * If the NFSv4.1 client just acquired its first open, write a timestamp
	 * to the stable storage file.
	 */
	if (gotstate != 0) {
		nfsrv_writestable(clidp, len, NFSNST_NEWSTATE, p);
		nfsrv_backupstable();
	}

out:
	free(clidp, M_TEMP);
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Open update. Does the confirm, downgrade and close.
 */
APPLESTATIC int
nfsrv_openupdate(vnode_t vp, struct nfsstate *new_stp, nfsquad_t clientid,
    nfsv4stateid_t *stateidp, struct nfsrv_descript *nd, NFSPROC_T *p,
    int *retwriteaccessp)
{
	struct nfsstate *stp;
	struct nfsclient *clp;
	struct nfslockfile *lfp;
	u_int32_t bits;
	int error = 0, gotstate = 0, len = 0;
	u_char *clidp = NULL;

	/*
	 * Check for restart conditions (client and server).
	 */
	error = nfsrv_checkrestart(clientid, new_stp->ls_flags,
	    &new_stp->ls_stateid, 0);
	if (error)
		goto out;

	clidp = malloc(NFSV4_OPAQUELIMIT, M_TEMP, M_WAITOK);
	NFSLOCKSTATE();
	/*
	 * Get the open structure via clientid and stateid.
	 */
	error = nfsrv_getclient(clientid, CLOPS_RENEW, &clp, NULL,
	    (nfsquad_t)((u_quad_t)0), 0, nd, p);
	if (!error)
		error = nfsrv_getstate(clp, &new_stp->ls_stateid,
		    new_stp->ls_flags, &stp);

	/*
	 * Sanity check the open.
	 */
	if (!error && (!(stp->ls_flags & NFSLCK_OPEN) ||
		(!(new_stp->ls_flags & NFSLCK_CONFIRM) &&
		 (stp->ls_openowner->ls_flags & NFSLCK_NEEDSCONFIRM)) ||
		((new_stp->ls_flags & NFSLCK_CONFIRM) &&
		 (!(stp->ls_openowner->ls_flags & NFSLCK_NEEDSCONFIRM)))))
		error = NFSERR_BADSTATEID;

	if (!error)
		error = nfsrv_checkseqid(nd, new_stp->ls_seq,
		    stp->ls_openowner, new_stp->ls_op);
	if (!error && stp->ls_stateid.seqid != new_stp->ls_stateid.seqid &&
	    (((nd->nd_flag & ND_NFSV41) == 0 &&
	      !(new_stp->ls_flags & NFSLCK_CONFIRM)) ||
	     ((nd->nd_flag & ND_NFSV41) != 0 &&
	      new_stp->ls_stateid.seqid != 0)))
		error = NFSERR_OLDSTATEID;
	if (!error && vnode_vtype(vp) != VREG) {
		if (vnode_vtype(vp) == VDIR)
			error = NFSERR_ISDIR;
		else
			error = NFSERR_INVAL;
	}

	if (error) {
		/*
		 * If a client tries to confirm an Open with a bad
		 * seqid# and there are no byte range locks or other Opens
		 * on the openowner, just throw it away, so the next use of the
		 * openowner will start a fresh seq#.
		 */
		if (error == NFSERR_BADSEQID &&
		    (new_stp->ls_flags & NFSLCK_CONFIRM) &&
		    nfsrv_nootherstate(stp))
			nfsrv_freeopenowner(stp->ls_openowner, 0, p);
		NFSUNLOCKSTATE();
		goto out;
	}

	/*
	 * Set the return stateid.
	 */
	stateidp->seqid = stp->ls_stateid.seqid + 1;
	if ((nd->nd_flag & ND_NFSV41) != 0 && stateidp->seqid == 0)
		stateidp->seqid = 1;
	stateidp->other[0] = stp->ls_stateid.other[0];
	stateidp->other[1] = stp->ls_stateid.other[1];
	stateidp->other[2] = stp->ls_stateid.other[2];
	/*
	 * Now, handle the three cases.
	 */
	if (new_stp->ls_flags & NFSLCK_CONFIRM) {
		/*
		 * If the open doesn't need confirmation, it seems to me that
		 * there is a client error, but I'll just log it and keep going?
		 */
		if (!(stp->ls_openowner->ls_flags & NFSLCK_NEEDSCONFIRM))
			printf("Nfsv4d: stray open confirm\n");
		stp->ls_openowner->ls_flags = 0;
		stp->ls_stateid.seqid++;
		if ((nd->nd_flag & ND_NFSV41) != 0 &&
		    stp->ls_stateid.seqid == 0)
			stp->ls_stateid.seqid = 1;
		if (!(clp->lc_flags & LCL_STAMPEDSTABLE)) {
			clp->lc_flags |= LCL_STAMPEDSTABLE;
			len = clp->lc_idlen;
			NFSBCOPY(clp->lc_id, clidp, len);
			gotstate = 1;
		}
		NFSUNLOCKSTATE();
	} else if (new_stp->ls_flags & NFSLCK_CLOSE) {
		lfp = stp->ls_lfp;
		if (retwriteaccessp != NULL) {
			if ((stp->ls_flags & NFSLCK_WRITEACCESS) != 0)
				*retwriteaccessp = 1;
			else
				*retwriteaccessp = 0;
		}
		if (nfsrv_dolocallocks != 0 && !LIST_EMPTY(&stp->ls_open)) {
			/* Get the lf lock */
			nfsrv_locklf(lfp);
			NFSUNLOCKSTATE();
			ASSERT_VOP_ELOCKED(vp, "nfsrv_openupdate");
			NFSVOPUNLOCK(vp, 0);
			if (nfsrv_freeopen(stp, vp, 1, p) == 0) {
				NFSLOCKSTATE();
				nfsrv_unlocklf(lfp);
				NFSUNLOCKSTATE();
			}
			NFSVOPLOCK(vp, LK_EXCLUSIVE | LK_RETRY);
		} else {
			(void) nfsrv_freeopen(stp, NULL, 0, p);
			NFSUNLOCKSTATE();
		}
	} else {
		/*
		 * Update the share bits, making sure that the new set are a
		 * subset of the old ones.
		 */
		bits = (new_stp->ls_flags & NFSLCK_SHAREBITS);
		if (~(stp->ls_flags) & bits) {
			NFSUNLOCKSTATE();
			error = NFSERR_INVAL;
			goto out;
		}
		stp->ls_flags = (bits | NFSLCK_OPEN);
		stp->ls_stateid.seqid++;
		if ((nd->nd_flag & ND_NFSV41) != 0 &&
		    stp->ls_stateid.seqid == 0)
			stp->ls_stateid.seqid = 1;
		NFSUNLOCKSTATE();
	}

	/*
	 * If the client just confirmed its first open, write a timestamp
	 * to the stable storage file.
	 */
	if (gotstate != 0) {
		nfsrv_writestable(clidp, len, NFSNST_NEWSTATE, p);
		nfsrv_backupstable();
	}

out:
	free(clidp, M_TEMP);
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Delegation update. Does the purge and return.
 */
APPLESTATIC int
nfsrv_delegupdate(struct nfsrv_descript *nd, nfsquad_t clientid,
    nfsv4stateid_t *stateidp, vnode_t vp, int op, struct ucred *cred,
    NFSPROC_T *p, int *retwriteaccessp)
{
	struct nfsstate *stp;
	struct nfsclient *clp;
	int error = 0;
	fhandle_t fh;

	/*
	 * Do a sanity check against the file handle for DelegReturn.
	 */
	if (vp) {
		error = nfsvno_getfh(vp, &fh, p);
		if (error)
			goto out;
	}
	/*
	 * Check for restart conditions (client and server).
	 */
	if (op == NFSV4OP_DELEGRETURN)
		error = nfsrv_checkrestart(clientid, NFSLCK_DELEGRETURN,
			stateidp, 0);
	else
		error = nfsrv_checkrestart(clientid, NFSLCK_DELEGPURGE,
			stateidp, 0);

	NFSLOCKSTATE();
	/*
	 * Get the open structure via clientid and stateid.
	 */
	if (!error)
	    error = nfsrv_getclient(clientid, CLOPS_RENEW, &clp, NULL,
		(nfsquad_t)((u_quad_t)0), 0, nd, p);
	if (error) {
		if (error == NFSERR_CBPATHDOWN)
			error = 0;
		if (error == NFSERR_STALECLIENTID && op == NFSV4OP_DELEGRETURN)
			error = NFSERR_STALESTATEID;
	}
	if (!error && op == NFSV4OP_DELEGRETURN) {
	    error = nfsrv_getstate(clp, stateidp, NFSLCK_DELEGRETURN, &stp);
	    if (!error && stp->ls_stateid.seqid != stateidp->seqid &&
		((nd->nd_flag & ND_NFSV41) == 0 || stateidp->seqid != 0))
		error = NFSERR_OLDSTATEID;
	}
	/*
	 * NFSERR_EXPIRED means that the state has gone away,
	 * so Delegations have been purged. Just return ok.
	 */
	if (error == NFSERR_EXPIRED && op == NFSV4OP_DELEGPURGE) {
		NFSUNLOCKSTATE();
		error = 0;
		goto out;
	}
	if (error) {
		NFSUNLOCKSTATE();
		goto out;
	}

	if (op == NFSV4OP_DELEGRETURN) {
		if (NFSBCMP((caddr_t)&fh, (caddr_t)&stp->ls_lfp->lf_fh,
		    sizeof (fhandle_t))) {
			NFSUNLOCKSTATE();
			error = NFSERR_BADSTATEID;
			goto out;
		}
		if (retwriteaccessp != NULL) {
			if ((stp->ls_flags & NFSLCK_DELEGWRITE) != 0)
				*retwriteaccessp = 1;
			else
				*retwriteaccessp = 0;
		}
		nfsrv_freedeleg(stp);
	} else {
		nfsrv_freedeleglist(&clp->lc_olddeleg);
	}
	NFSUNLOCKSTATE();
	error = 0;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Release lock owner.
 */
APPLESTATIC int
nfsrv_releaselckown(struct nfsstate *new_stp, nfsquad_t clientid,
    NFSPROC_T *p)
{
	struct nfsstate *stp, *nstp, *openstp, *ownstp;
	struct nfsclient *clp;
	int error = 0;

	/*
	 * Check for restart conditions (client and server).
	 */
	error = nfsrv_checkrestart(clientid, new_stp->ls_flags,
	    &new_stp->ls_stateid, 0);
	if (error)
		goto out;

	NFSLOCKSTATE();
	/*
	 * Get the lock owner by name.
	 */
	error = nfsrv_getclient(clientid, CLOPS_RENEW, &clp, NULL,
	    (nfsquad_t)((u_quad_t)0), 0, NULL, p);
	if (error) {
		NFSUNLOCKSTATE();
		goto out;
	}
	LIST_FOREACH(ownstp, &clp->lc_open, ls_list) {
	    LIST_FOREACH(openstp, &ownstp->ls_open, ls_list) {
		stp = LIST_FIRST(&openstp->ls_open);
		while (stp != LIST_END(&openstp->ls_open)) {
		    nstp = LIST_NEXT(stp, ls_list);
		    /*
		     * If the owner matches, check for locks and
		     * then free or return an error.
		     */
		    if (stp->ls_ownerlen == new_stp->ls_ownerlen &&
			!NFSBCMP(stp->ls_owner, new_stp->ls_owner,
			 stp->ls_ownerlen)){
			if (LIST_EMPTY(&stp->ls_lock)) {
			    nfsrv_freelockowner(stp, NULL, 0, p);
			} else {
			    NFSUNLOCKSTATE();
			    error = NFSERR_LOCKSHELD;
			    goto out;
			}
		    }
		    stp = nstp;
		}
	    }
	}
	NFSUNLOCKSTATE();

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Get the file handle for a lock structure.
 */
static int
nfsrv_getlockfh(vnode_t vp, u_short flags, struct nfslockfile *new_lfp,
    fhandle_t *nfhp, NFSPROC_T *p)
{
	fhandle_t *fhp = NULL;
	int error;

	/*
	 * For lock, use the new nfslock structure, otherwise just
	 * a fhandle_t on the stack.
	 */
	if (flags & NFSLCK_OPEN) {
		KASSERT(new_lfp != NULL, ("nfsrv_getlockfh: new_lfp NULL"));
		fhp = &new_lfp->lf_fh;
	} else if (nfhp) {
		fhp = nfhp;
	} else {
		panic("nfsrv_getlockfh");
	}
	error = nfsvno_getfh(vp, fhp, p);
	NFSEXITCODE(error);
	return (error);
}

/*
 * Get an nfs lock structure. Allocate one, as required, and return a
 * pointer to it.
 * Returns an NFSERR_xxx upon failure or -1 to indicate no current lock.
 */
static int
nfsrv_getlockfile(u_short flags, struct nfslockfile **new_lfpp,
    struct nfslockfile **lfpp, fhandle_t *nfhp, int lockit)
{
	struct nfslockfile *lfp;
	fhandle_t *fhp = NULL, *tfhp;
	struct nfslockhashhead *hp;
	struct nfslockfile *new_lfp = NULL;

	/*
	 * For lock, use the new nfslock structure, otherwise just
	 * a fhandle_t on the stack.
	 */
	if (flags & NFSLCK_OPEN) {
		new_lfp = *new_lfpp;
		fhp = &new_lfp->lf_fh;
	} else if (nfhp) {
		fhp = nfhp;
	} else {
		panic("nfsrv_getlockfile");
	}

	hp = NFSLOCKHASH(fhp);
	LIST_FOREACH(lfp, hp, lf_hash) {
		tfhp = &lfp->lf_fh;
		if (NFSVNO_CMPFH(fhp, tfhp)) {
			if (lockit)
				nfsrv_locklf(lfp);
			*lfpp = lfp;
			return (0);
		}
	}
	if (!(flags & NFSLCK_OPEN))
		return (-1);

	/*
	 * No match, so chain the new one into the list.
	 */
	LIST_INIT(&new_lfp->lf_open);
	LIST_INIT(&new_lfp->lf_lock);
	LIST_INIT(&new_lfp->lf_deleg);
	LIST_INIT(&new_lfp->lf_locallock);
	LIST_INIT(&new_lfp->lf_rollback);
	new_lfp->lf_locallock_lck.nfslock_usecnt = 0;
	new_lfp->lf_locallock_lck.nfslock_lock = 0;
	new_lfp->lf_usecount = 0;
	LIST_INSERT_HEAD(hp, new_lfp, lf_hash);
	*lfpp = new_lfp;
	*new_lfpp = NULL;
	return (0);
}

/*
 * This function adds a nfslock lock structure to the list for the associated
 * nfsstate and nfslockfile structures. It will be inserted after the
 * entry pointed at by insert_lop.
 */
static void
nfsrv_insertlock(struct nfslock *new_lop, struct nfslock *insert_lop,
    struct nfsstate *stp, struct nfslockfile *lfp)
{
	struct nfslock *lop, *nlop;

	new_lop->lo_stp = stp;
	new_lop->lo_lfp = lfp;

	if (stp != NULL) {
		/* Insert in increasing lo_first order */
		lop = LIST_FIRST(&lfp->lf_lock);
		if (lop == LIST_END(&lfp->lf_lock) ||
		    new_lop->lo_first <= lop->lo_first) {
			LIST_INSERT_HEAD(&lfp->lf_lock, new_lop, lo_lckfile);
		} else {
			nlop = LIST_NEXT(lop, lo_lckfile);
			while (nlop != LIST_END(&lfp->lf_lock) &&
			       nlop->lo_first < new_lop->lo_first) {
				lop = nlop;
				nlop = LIST_NEXT(lop, lo_lckfile);
			}
			LIST_INSERT_AFTER(lop, new_lop, lo_lckfile);
		}
	} else {
		new_lop->lo_lckfile.le_prev = NULL;	/* list not used */
	}

	/*
	 * Insert after insert_lop, which is overloaded as stp or lfp for
	 * an empty list.
	 */
	if (stp == NULL && (struct nfslockfile *)insert_lop == lfp)
		LIST_INSERT_HEAD(&lfp->lf_locallock, new_lop, lo_lckowner);
	else if ((struct nfsstate *)insert_lop == stp)
		LIST_INSERT_HEAD(&stp->ls_lock, new_lop, lo_lckowner);
	else
		LIST_INSERT_AFTER(insert_lop, new_lop, lo_lckowner);
	if (stp != NULL) {
		nfsstatsv1.srvlocks++;
		nfsrv_openpluslock++;
	}
}

/*
 * This function updates the locking for a lock owner and given file. It
 * maintains a list of lock ranges ordered on increasing file offset that
 * are NFSLCK_READ or NFSLCK_WRITE and non-overlapping (aka POSIX style).
 * It always adds new_lop to the list and sometimes uses the one pointed
 * at by other_lopp.
 */
static void
nfsrv_updatelock(struct nfsstate *stp, struct nfslock **new_lopp,
    struct nfslock **other_lopp, struct nfslockfile *lfp)
{
	struct nfslock *new_lop = *new_lopp;
	struct nfslock *lop, *tlop, *ilop;
	struct nfslock *other_lop = *other_lopp;
	int unlock = 0, myfile = 0;
	u_int64_t tmp;

	/*
	 * Work down the list until the lock is merged.
	 */
	if (new_lop->lo_flags & NFSLCK_UNLOCK)
		unlock = 1;
	if (stp != NULL) {
		ilop = (struct nfslock *)stp;
		lop = LIST_FIRST(&stp->ls_lock);
	} else {
		ilop = (struct nfslock *)lfp;
		lop = LIST_FIRST(&lfp->lf_locallock);
	}
	while (lop != NULL) {
	    /*
	     * Only check locks for this file that aren't before the start of
	     * new lock's range.
	     */
	    if (lop->lo_lfp == lfp) {
	      myfile = 1;
	      if (lop->lo_end >= new_lop->lo_first) {
		if (new_lop->lo_end < lop->lo_first) {
			/*
			 * If the new lock ends before the start of the
			 * current lock's range, no merge, just insert
			 * the new lock.
			 */
			break;
		}
		if (new_lop->lo_flags == lop->lo_flags ||
		    (new_lop->lo_first <= lop->lo_first &&
		     new_lop->lo_end >= lop->lo_end)) {
			/*
			 * This lock can be absorbed by the new lock/unlock.
			 * This happens when it covers the entire range
			 * of the old lock or is contiguous
			 * with the old lock and is of the same type or an
			 * unlock.
			 */
			if (lop->lo_first < new_lop->lo_first)
				new_lop->lo_first = lop->lo_first;
			if (lop->lo_end > new_lop->lo_end)
				new_lop->lo_end = lop->lo_end;
			tlop = lop;
			lop = LIST_NEXT(lop, lo_lckowner);
			nfsrv_freenfslock(tlop);
			continue;
		}

		/*
		 * All these cases are for contiguous locks that are not the
		 * same type, so they can't be merged.
		 */
		if (new_lop->lo_first <= lop->lo_first) {
			/*
			 * This case is where the new lock overlaps with the
			 * first part of the old lock. Move the start of the
			 * old lock to just past the end of the new lock. The
			 * new lock will be inserted in front of the old, since
			 * ilop hasn't been updated. (We are done now.)
			 */
			lop->lo_first = new_lop->lo_end;
			break;
		}
		if (new_lop->lo_end >= lop->lo_end) {
			/*
			 * This case is where the new lock overlaps with the
			 * end of the old lock's range. Move the old lock's
			 * end to just before the new lock's first and insert
			 * the new lock after the old lock.
			 * Might not be done yet, since the new lock could
			 * overlap further locks with higher ranges.
			 */
			lop->lo_end = new_lop->lo_first;
			ilop = lop;
			lop = LIST_NEXT(lop, lo_lckowner);
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
		 * new_lop->lo_flags any longer.
		 */
		tmp = new_lop->lo_first;
		if (other_lop == NULL) {
			if (!unlock)
				panic("nfsd srv update unlock");
			other_lop = new_lop;
			*new_lopp = NULL;
		}
		other_lop->lo_first = new_lop->lo_end;
		other_lop->lo_end = lop->lo_end;
		other_lop->lo_flags = lop->lo_flags;
		other_lop->lo_stp = stp;
		other_lop->lo_lfp = lfp;
		lop->lo_end = tmp;
		nfsrv_insertlock(other_lop, lop, stp, lfp);
		*other_lopp = NULL;
		ilop = lop;
		break;
	      }
	    }
	    ilop = lop;
	    lop = LIST_NEXT(lop, lo_lckowner);
	    if (myfile && (lop == NULL || lop->lo_lfp != lfp))
		break;
	}

	/*
	 * Insert the new lock in the list at the appropriate place.
	 */
	if (!unlock) {
		nfsrv_insertlock(new_lop, ilop, stp, lfp);
		*new_lopp = NULL;
	}
}

/*
 * This function handles sequencing of locks, etc.
 * It returns an error that indicates what the caller should do.
 */
static int
nfsrv_checkseqid(struct nfsrv_descript *nd, u_int32_t seqid,
    struct nfsstate *stp, struct nfsrvcache *op)
{
	int error = 0;

	if ((nd->nd_flag & ND_NFSV41) != 0)
		/* NFSv4.1 ignores the open_seqid and lock_seqid. */
		goto out;
	if (op != nd->nd_rp)
		panic("nfsrvstate checkseqid");
	if (!(op->rc_flag & RC_INPROG))
		panic("nfsrvstate not inprog");
	if (stp->ls_op && stp->ls_op->rc_refcnt <= 0) {
		printf("refcnt=%d\n", stp->ls_op->rc_refcnt);
		panic("nfsrvstate op refcnt");
	}
	if ((stp->ls_seq + 1) == seqid) {
		if (stp->ls_op)
			nfsrvd_derefcache(stp->ls_op);
		stp->ls_op = op;
		nfsrvd_refcache(op);
		stp->ls_seq = seqid;
		goto out;
	} else if (stp->ls_seq == seqid && stp->ls_op &&
		op->rc_xid == stp->ls_op->rc_xid &&
		op->rc_refcnt == 0 &&
		op->rc_reqlen == stp->ls_op->rc_reqlen &&
		op->rc_cksum == stp->ls_op->rc_cksum) {
		if (stp->ls_op->rc_flag & RC_INPROG) {
			error = NFSERR_DONTREPLY;
			goto out;
		}
		nd->nd_rp = stp->ls_op;
		nd->nd_rp->rc_flag |= RC_INPROG;
		nfsrvd_delcache(op);
		error = NFSERR_REPLYFROMCACHE;
		goto out;
	}
	error = NFSERR_BADSEQID;

out:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Get the client ip address for callbacks. If the strings can't be parsed,
 * just set lc_program to 0 to indicate no callbacks are possible.
 * (For cases where the address can't be parsed or is 0.0.0.0.0.0, set
 *  the address to the client's transport address. This won't be used
 *  for callbacks, but can be printed out by nfsstats for info.)
 * Return error if the xdr can't be parsed, 0 otherwise.
 */
APPLESTATIC int
nfsrv_getclientipaddr(struct nfsrv_descript *nd, struct nfsclient *clp)
{
	u_int32_t *tl;
	u_char *cp, *cp2;
	int i, j;
	struct sockaddr_in *rad, *sad;
	u_char protocol[5], addr[24];
	int error = 0, cantparse = 0;
	union {
		in_addr_t ival;
		u_char cval[4];
	} ip;
	union {
		in_port_t sval;
		u_char cval[2];
	} port;

	rad = NFSSOCKADDR(clp->lc_req.nr_nam, struct sockaddr_in *);
	rad->sin_family = AF_INET;
	rad->sin_len = sizeof (struct sockaddr_in);
	rad->sin_addr.s_addr = 0;
	rad->sin_port = 0;
	clp->lc_req.nr_client = NULL;
	clp->lc_req.nr_lock = 0;
	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	i = fxdr_unsigned(int, *tl);
	if (i >= 3 && i <= 4) {
		error = nfsrv_mtostr(nd, protocol, i);
		if (error)
			goto nfsmout;
		if (!strcmp(protocol, "tcp")) {
			clp->lc_flags |= LCL_TCPCALLBACK;
			clp->lc_req.nr_sotype = SOCK_STREAM;
			clp->lc_req.nr_soproto = IPPROTO_TCP;
		} else if (!strcmp(protocol, "udp")) {
			clp->lc_req.nr_sotype = SOCK_DGRAM;
			clp->lc_req.nr_soproto = IPPROTO_UDP;
		} else {
			cantparse = 1;
		}
	} else {
		cantparse = 1;
		if (i > 0) {
			error = nfsm_advance(nd, NFSM_RNDUP(i), -1);
			if (error)
				goto nfsmout;
		}
	}
	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	i = fxdr_unsigned(int, *tl);
	if (i < 0) {
		error = NFSERR_BADXDR;
		goto nfsmout;
	} else if (i == 0) {
		cantparse = 1;
	} else if (!cantparse && i <= 23 && i >= 11) {
		error = nfsrv_mtostr(nd, addr, i);
		if (error)
			goto nfsmout;

		/*
		 * Parse out the address fields. We expect 6 decimal numbers
		 * separated by '.'s.
		 */
		cp = addr;
		i = 0;
		while (*cp && i < 6) {
			cp2 = cp;
			while (*cp2 && *cp2 != '.')
				cp2++;
			if (*cp2)
				*cp2++ = '\0';
			else if (i != 5) {
				cantparse = 1;
				break;
			}
			j = nfsrv_getipnumber(cp);
			if (j >= 0) {
				if (i < 4)
					ip.cval[3 - i] = j;
				else
					port.cval[5 - i] = j;
			} else {
				cantparse = 1;
				break;
			}
			cp = cp2;
			i++;
		}
		if (!cantparse) {
			if (ip.ival != 0x0) {
				rad->sin_addr.s_addr = htonl(ip.ival);
				rad->sin_port = htons(port.sval);
			} else {
				cantparse = 1;
			}
		}
	} else {
		cantparse = 1;
		if (i > 0) {
			error = nfsm_advance(nd, NFSM_RNDUP(i), -1);
			if (error)
				goto nfsmout;
		}
	}
	if (cantparse) {
		sad = NFSSOCKADDR(nd->nd_nam, struct sockaddr_in *);
		if (sad->sin_family == AF_INET) {
			rad->sin_addr.s_addr = sad->sin_addr.s_addr;
			rad->sin_port = 0x0;
		}
		clp->lc_program = 0;
	}
nfsmout:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Turn a string of up to three decimal digits into a number. Return -1 upon
 * error.
 */
static int
nfsrv_getipnumber(u_char *cp)
{
	int i = 0, j = 0;

	while (*cp) {
		if (j > 2 || *cp < '0' || *cp > '9')
			return (-1);
		i *= 10;
		i += (*cp - '0');
		cp++;
		j++;
	}
	if (i < 256)
		return (i);
	return (-1);
}

/*
 * This function checks for restart conditions.
 */
static int
nfsrv_checkrestart(nfsquad_t clientid, u_int32_t flags,
    nfsv4stateid_t *stateidp, int specialid)
{
	int ret = 0;

	/*
	 * First check for a server restart. Open, LockT, ReleaseLockOwner
	 * and DelegPurge have a clientid, the rest a stateid.
	 */
	if (flags &
	    (NFSLCK_OPEN | NFSLCK_TEST | NFSLCK_RELEASE | NFSLCK_DELEGPURGE)) {
		if (clientid.lval[0] != nfsrvboottime) {
			ret = NFSERR_STALECLIENTID;
			goto out;
		}
	} else if (stateidp->other[0] != nfsrvboottime &&
		specialid == 0) {
		ret = NFSERR_STALESTATEID;
		goto out;
	}

	/*
	 * Read, Write, Setattr and LockT can return NFSERR_GRACE and do
	 * not use a lock/open owner seqid#, so the check can be done now.
	 * (The others will be checked, as required, later.)
	 */
	if (!(flags & (NFSLCK_CHECK | NFSLCK_TEST)))
		goto out;

	NFSLOCKSTATE();
	ret = nfsrv_checkgrace(NULL, NULL, flags);
	NFSUNLOCKSTATE();

out:
	NFSEXITCODE(ret);
	return (ret);
}

/*
 * Check for grace.
 */
static int
nfsrv_checkgrace(struct nfsrv_descript *nd, struct nfsclient *clp,
    u_int32_t flags)
{
	int error = 0, notreclaimed;
	struct nfsrv_stable *sp;

	if ((nfsrv_stablefirst.nsf_flags & (NFSNSF_UPDATEDONE |
	     NFSNSF_GRACEOVER)) == 0) {
		/*
		 * First, check to see if all of the clients have done a
		 * ReclaimComplete.  If so, grace can end now.
		 */
		notreclaimed = 0;
		LIST_FOREACH(sp, &nfsrv_stablefirst.nsf_head, nst_list) {
			if ((sp->nst_flag & NFSNST_RECLAIMED) == 0) {
				notreclaimed = 1;
				break;
			}
		}
		if (notreclaimed == 0)
			nfsrv_stablefirst.nsf_flags |= (NFSNSF_GRACEOVER |
			    NFSNSF_NEEDLOCK);
	}

	if ((nfsrv_stablefirst.nsf_flags & NFSNSF_GRACEOVER) != 0) {
		if (flags & NFSLCK_RECLAIM) {
			error = NFSERR_NOGRACE;
			goto out;
		}
	} else {
		if (!(flags & NFSLCK_RECLAIM)) {
			error = NFSERR_GRACE;
			goto out;
		}
		if (nd != NULL && clp != NULL &&
		    (nd->nd_flag & ND_NFSV41) != 0 &&
		    (clp->lc_flags & LCL_RECLAIMCOMPLETE) != 0) {
			error = NFSERR_NOGRACE;
			goto out;
		}

		/*
		 * If grace is almost over and we are still getting Reclaims,
		 * extend grace a bit.
		 */
		if ((NFSD_MONOSEC + NFSRV_LEASEDELTA) >
		    nfsrv_stablefirst.nsf_eograce)
			nfsrv_stablefirst.nsf_eograce = NFSD_MONOSEC +
				NFSRV_LEASEDELTA;
	}

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Do a server callback.
 * The "trunc" argument is slightly overloaded and refers to different
 * boolean arguments for CBRECALL and CBLAYOUTRECALL.
 */
static int
nfsrv_docallback(struct nfsclient *clp, int procnum, nfsv4stateid_t *stateidp,
    int trunc, fhandle_t *fhp, struct nfsvattr *nap, nfsattrbit_t *attrbitp,
    int laytype, NFSPROC_T *p)
{
	mbuf_t m;
	u_int32_t *tl;
	struct nfsrv_descript *nd;
	struct ucred *cred;
	int error = 0;
	u_int32_t callback;
	struct nfsdsession *sep = NULL;
	uint64_t tval;

	nd = malloc(sizeof(*nd), M_TEMP, M_WAITOK | M_ZERO);
	cred = newnfs_getcred();
	NFSLOCKSTATE();	/* mostly for lc_cbref++ */
	if (clp->lc_flags & LCL_NEEDSCONFIRM) {
		NFSUNLOCKSTATE();
		panic("docallb");
	}
	clp->lc_cbref++;

	/*
	 * Fill the callback program# and version into the request
	 * structure for newnfs_connect() to use.
	 */
	clp->lc_req.nr_prog = clp->lc_program;
#ifdef notnow
	if ((clp->lc_flags & LCL_NFSV41) != 0)
		clp->lc_req.nr_vers = NFSV41_CBVERS;
	else
#endif
		clp->lc_req.nr_vers = NFSV4_CBVERS;

	/*
	 * First, fill in some of the fields of nd and cr.
	 */
	nd->nd_flag = ND_NFSV4;
	if (clp->lc_flags & LCL_GSS)
		nd->nd_flag |= ND_KERBV;
	if ((clp->lc_flags & LCL_NFSV41) != 0)
		nd->nd_flag |= ND_NFSV41;
	nd->nd_repstat = 0;
	cred->cr_uid = clp->lc_uid;
	cred->cr_gid = clp->lc_gid;
	callback = clp->lc_callback;
	NFSUNLOCKSTATE();
	cred->cr_ngroups = 1;

	/*
	 * Get the first mbuf for the request.
	 */
	MGET(m, M_WAITOK, MT_DATA);
	mbuf_setlen(m, 0);
	nd->nd_mreq = nd->nd_mb = m;
	nd->nd_bpos = NFSMTOD(m, caddr_t);
	
	/*
	 * and build the callback request.
	 */
	if (procnum == NFSV4OP_CBGETATTR) {
		nd->nd_procnum = NFSV4PROC_CBCOMPOUND;
		error = nfsrv_cbcallargs(nd, clp, callback, NFSV4OP_CBGETATTR,
		    "CB Getattr", &sep);
		if (error != 0) {
			mbuf_freem(nd->nd_mreq);
			goto errout;
		}
		(void)nfsm_fhtom(nd, (u_int8_t *)fhp, NFSX_MYFH, 0);
		(void)nfsrv_putattrbit(nd, attrbitp);
	} else if (procnum == NFSV4OP_CBRECALL) {
		nd->nd_procnum = NFSV4PROC_CBCOMPOUND;
		error = nfsrv_cbcallargs(nd, clp, callback, NFSV4OP_CBRECALL,
		    "CB Recall", &sep);
		if (error != 0) {
			mbuf_freem(nd->nd_mreq);
			goto errout;
		}
		NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED + NFSX_STATEID);
		*tl++ = txdr_unsigned(stateidp->seqid);
		NFSBCOPY((caddr_t)stateidp->other, (caddr_t)tl,
		    NFSX_STATEIDOTHER);
		tl += (NFSX_STATEIDOTHER / NFSX_UNSIGNED);
		if (trunc)
			*tl = newnfs_true;
		else
			*tl = newnfs_false;
		(void)nfsm_fhtom(nd, (u_int8_t *)fhp, NFSX_MYFH, 0);
	} else if (procnum == NFSV4OP_CBLAYOUTRECALL) {
		NFSD_DEBUG(4, "docallback layout recall\n");
		nd->nd_procnum = NFSV4PROC_CBCOMPOUND;
		error = nfsrv_cbcallargs(nd, clp, callback,
		    NFSV4OP_CBLAYOUTRECALL, "CB Reclayout", &sep);
		NFSD_DEBUG(4, "aft cbcallargs=%d\n", error);
		if (error != 0) {
			mbuf_freem(nd->nd_mreq);
			goto errout;
		}
		NFSM_BUILD(tl, u_int32_t *, 4 * NFSX_UNSIGNED);
		*tl++ = txdr_unsigned(laytype);
		*tl++ = txdr_unsigned(NFSLAYOUTIOMODE_ANY);
		if (trunc)
			*tl++ = newnfs_true;
		else
			*tl++ = newnfs_false;
		*tl = txdr_unsigned(NFSV4LAYOUTRET_FILE);
		nfsm_fhtom(nd, (uint8_t *)fhp, NFSX_MYFH, 0);
		NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_HYPER + NFSX_STATEID);
		tval = 0;
		txdr_hyper(tval, tl); tl += 2;
		tval = UINT64_MAX;
		txdr_hyper(tval, tl); tl += 2;
		*tl++ = txdr_unsigned(stateidp->seqid);
		NFSBCOPY(stateidp->other, tl, NFSX_STATEIDOTHER);
		tl += (NFSX_STATEIDOTHER / NFSX_UNSIGNED);
		NFSD_DEBUG(4, "aft args\n");
	} else if (procnum == NFSV4PROC_CBNULL) {
		nd->nd_procnum = NFSV4PROC_CBNULL;
		if ((clp->lc_flags & LCL_NFSV41) != 0) {
			error = nfsv4_getcbsession(clp, &sep);
			if (error != 0) {
				mbuf_freem(nd->nd_mreq);
				goto errout;
			}
		}
	} else {
		error = NFSERR_SERVERFAULT;
		mbuf_freem(nd->nd_mreq);
		goto errout;
	}

	/*
	 * Call newnfs_connect(), as required, and then newnfs_request().
	 */
	(void) newnfs_sndlock(&clp->lc_req.nr_lock);
	if (clp->lc_req.nr_client == NULL) {
		if ((clp->lc_flags & LCL_NFSV41) != 0) {
			error = ECONNREFUSED;
			nfsrv_freesession(sep, NULL);
		} else if (nd->nd_procnum == NFSV4PROC_CBNULL)
			error = newnfs_connect(NULL, &clp->lc_req, cred,
			    NULL, 1);
		else
			error = newnfs_connect(NULL, &clp->lc_req, cred,
			    NULL, 3);
	}
	newnfs_sndunlock(&clp->lc_req.nr_lock);
	NFSD_DEBUG(4, "aft sndunlock=%d\n", error);
	if (!error) {
		if ((nd->nd_flag & ND_NFSV41) != 0) {
			KASSERT(sep != NULL, ("sep NULL"));
			if (sep->sess_cbsess.nfsess_xprt != NULL)
				error = newnfs_request(nd, NULL, clp,
				    &clp->lc_req, NULL, NULL, cred,
				    clp->lc_program, clp->lc_req.nr_vers, NULL,
				    1, NULL, &sep->sess_cbsess);
			else {
				/*
				 * This should probably never occur, but if a
				 * client somehow does an RPC without a
				 * SequenceID Op that causes a callback just
				 * after the nfsd threads have been terminated
				 * and restared we could conceivably get here
				 * without a backchannel xprt.
				 */
				printf("nfsrv_docallback: no xprt\n");
				error = ECONNREFUSED;
			}
			NFSD_DEBUG(4, "aft newnfs_request=%d\n", error);
			nfsrv_freesession(sep, NULL);
		} else
			error = newnfs_request(nd, NULL, clp, &clp->lc_req,
			    NULL, NULL, cred, clp->lc_program,
			    clp->lc_req.nr_vers, NULL, 1, NULL, NULL);
	}
errout:
	NFSFREECRED(cred);

	/*
	 * If error is set here, the Callback path isn't working
	 * properly, so twiddle the appropriate LCL_ flags.
	 * (nd_repstat != 0 indicates the Callback path is working,
	 *  but the callback failed on the client.)
	 */
	if (error) {
		/*
		 * Mark the callback pathway down, which disabled issuing
		 * of delegations and gets Renew to return NFSERR_CBPATHDOWN.
		 */
		NFSLOCKSTATE();
		clp->lc_flags |= LCL_CBDOWN;
		NFSUNLOCKSTATE();
	} else {
		/*
		 * Callback worked. If the callback path was down, disable
		 * callbacks, so no more delegations will be issued. (This
		 * is done on the assumption that the callback pathway is
		 * flakey.)
		 */
		NFSLOCKSTATE();
		if (clp->lc_flags & LCL_CBDOWN)
			clp->lc_flags &= ~(LCL_CBDOWN | LCL_CALLBACKSON);
		NFSUNLOCKSTATE();
		if (nd->nd_repstat) {
			error = nd->nd_repstat;
			NFSD_DEBUG(1, "nfsrv_docallback op=%d err=%d\n",
			    procnum, error);
		} else if (error == 0 && procnum == NFSV4OP_CBGETATTR)
			error = nfsv4_loadattr(nd, NULL, nap, NULL, NULL, 0,
			    NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL,
			    p, NULL);
		mbuf_freem(nd->nd_mrep);
	}
	NFSLOCKSTATE();
	clp->lc_cbref--;
	if ((clp->lc_flags & LCL_WAKEUPWANTED) && clp->lc_cbref == 0) {
		clp->lc_flags &= ~LCL_WAKEUPWANTED;
		wakeup(clp);
	}
	NFSUNLOCKSTATE();

	free(nd, M_TEMP);
	NFSEXITCODE(error);
	return (error);
}

/*
 * Set up the compound RPC for the callback.
 */
static int
nfsrv_cbcallargs(struct nfsrv_descript *nd, struct nfsclient *clp,
    uint32_t callback, int op, const char *optag, struct nfsdsession **sepp)
{
	uint32_t *tl;
	int error, len;

	len = strlen(optag);
	(void)nfsm_strtom(nd, optag, len);
	NFSM_BUILD(tl, uint32_t *, 4 * NFSX_UNSIGNED);
	if ((nd->nd_flag & ND_NFSV41) != 0) {
		*tl++ = txdr_unsigned(NFSV41_MINORVERSION);
		*tl++ = txdr_unsigned(callback);
		*tl++ = txdr_unsigned(2);
		*tl = txdr_unsigned(NFSV4OP_CBSEQUENCE);
		error = nfsv4_setcbsequence(nd, clp, 1, sepp);
		if (error != 0)
			return (error);
		NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
		*tl = txdr_unsigned(op);
	} else {
		*tl++ = txdr_unsigned(NFSV4_MINORVERSION);
		*tl++ = txdr_unsigned(callback);
		*tl++ = txdr_unsigned(1);
		*tl = txdr_unsigned(op);
	}
	return (0);
}

/*
 * Return the next index# for a clientid. Mostly just increment and return
 * the next one, but... if the 32bit unsigned does actually wrap around,
 * it should be rebooted.
 * At an average rate of one new client per second, it will wrap around in
 * approximately 136 years. (I think the server will have been shut
 * down or rebooted before then.)
 */
static u_int32_t
nfsrv_nextclientindex(void)
{
	static u_int32_t client_index = 0;

	client_index++;
	if (client_index != 0)
		return (client_index);

	printf("%s: out of clientids\n", __func__);
	return (client_index);
}

/*
 * Return the next index# for a stateid. Mostly just increment and return
 * the next one, but... if the 32bit unsigned does actually wrap around
 * (will a BSD server stay up that long?), find
 * new start and end values.
 */
static u_int32_t
nfsrv_nextstateindex(struct nfsclient *clp)
{
	struct nfsstate *stp;
	int i;
	u_int32_t canuse, min_index, max_index;

	if (!(clp->lc_flags & LCL_INDEXNOTOK)) {
		clp->lc_stateindex++;
		if (clp->lc_stateindex != clp->lc_statemaxindex)
			return (clp->lc_stateindex);
	}

	/*
	 * Yuck, we've hit the end.
	 * Look for a new min and max.
	 */
	min_index = 0;
	max_index = 0xffffffff;
	for (i = 0; i < nfsrv_statehashsize; i++) {
	    LIST_FOREACH(stp, &clp->lc_stateid[i], ls_hash) {
		if (stp->ls_stateid.other[2] > 0x80000000) {
		    if (stp->ls_stateid.other[2] < max_index)
			max_index = stp->ls_stateid.other[2];
		} else {
		    if (stp->ls_stateid.other[2] > min_index)
			min_index = stp->ls_stateid.other[2];
		}
	    }
	}

	/*
	 * Yikes, highly unlikely, but I'll handle it anyhow.
	 */
	if (min_index == 0x80000000 && max_index == 0x80000001) {
	    canuse = 0;
	    /*
	     * Loop around until we find an unused entry. Return that
	     * and set LCL_INDEXNOTOK, so the search will continue next time.
	     * (This is one of those rare cases where a goto is the
	     *  cleanest way to code the loop.)
	     */
tryagain:
	    for (i = 0; i < nfsrv_statehashsize; i++) {
		LIST_FOREACH(stp, &clp->lc_stateid[i], ls_hash) {
		    if (stp->ls_stateid.other[2] == canuse) {
			canuse++;
			goto tryagain;
		    }
		}
	    }
	    clp->lc_flags |= LCL_INDEXNOTOK;
	    return (canuse);
	}

	/*
	 * Ok to start again from min + 1.
	 */
	clp->lc_stateindex = min_index + 1;
	clp->lc_statemaxindex = max_index;
	clp->lc_flags &= ~LCL_INDEXNOTOK;
	return (clp->lc_stateindex);
}

/*
 * The following functions handle the stable storage file that deals with
 * the edge conditions described in RFC3530 Sec. 8.6.3.
 * The file is as follows:
 * - a single record at the beginning that has the lease time of the
 *   previous server instance (before the last reboot) and the nfsrvboottime
 *   values for the previous server boots.
 *   These previous boot times are used to ensure that the current
 *   nfsrvboottime does not, somehow, get set to a previous one.
 *   (This is important so that Stale ClientIDs and StateIDs can
 *    be recognized.)
 *   The number of previous nfsvrboottime values precedes the list.
 * - followed by some number of appended records with:
 *   - client id string
 *   - flag that indicates it is a record revoking state via lease
 *     expiration or similar
 *     OR has successfully acquired state.
 * These structures vary in length, with the client string at the end, up
 * to NFSV4_OPAQUELIMIT in size.
 *
 * At the end of the grace period, the file is truncated, the first
 * record is rewritten with updated information and any acquired state
 * records for successful reclaims of state are written.
 *
 * Subsequent records are appended when the first state is issued to
 * a client and when state is revoked for a client.
 *
 * When reading the file in, state issued records that come later in
 * the file override older ones, since the append log is in cronological order.
 * If, for some reason, the file can't be read, the grace period is
 * immediately terminated and all reclaims get NFSERR_NOGRACE.
 */

/*
 * Read in the stable storage file. Called by nfssvc() before the nfsd
 * processes start servicing requests.
 */
APPLESTATIC void
nfsrv_setupstable(NFSPROC_T *p)
{
	struct nfsrv_stablefirst *sf = &nfsrv_stablefirst;
	struct nfsrv_stable *sp, *nsp;
	struct nfst_rec *tsp;
	int error, i, tryagain;
	off_t off = 0;
	ssize_t aresid, len;

	/*
	 * If NFSNSF_UPDATEDONE is set, this is a restart of the nfsds without
	 * a reboot, so state has not been lost.
	 */
	if (sf->nsf_flags & NFSNSF_UPDATEDONE)
		return;
	/*
	 * Set Grace over just until the file reads successfully.
	 */
	nfsrvboottime = time_second;
	LIST_INIT(&sf->nsf_head);
	sf->nsf_flags = (NFSNSF_GRACEOVER | NFSNSF_NEEDLOCK);
	sf->nsf_eograce = NFSD_MONOSEC + NFSRV_LEASEDELTA;
	if (sf->nsf_fp == NULL)
		return;
	error = NFSD_RDWR(UIO_READ, NFSFPVNODE(sf->nsf_fp),
	    (caddr_t)&sf->nsf_rec, sizeof (struct nfsf_rec), off, UIO_SYSSPACE,
	    0, NFSFPCRED(sf->nsf_fp), &aresid, p);
	if (error || aresid || sf->nsf_numboots == 0 ||
		sf->nsf_numboots > NFSNSF_MAXNUMBOOTS)
		return;

	/*
	 * Now, read in the boottimes.
	 */
	sf->nsf_bootvals = (time_t *)malloc((sf->nsf_numboots + 1) *
		sizeof (time_t), M_TEMP, M_WAITOK);
	off = sizeof (struct nfsf_rec);
	error = NFSD_RDWR(UIO_READ, NFSFPVNODE(sf->nsf_fp),
	    (caddr_t)sf->nsf_bootvals, sf->nsf_numboots * sizeof (time_t), off,
	    UIO_SYSSPACE, 0, NFSFPCRED(sf->nsf_fp), &aresid, p);
	if (error || aresid) {
		free(sf->nsf_bootvals, M_TEMP);
		sf->nsf_bootvals = NULL;
		return;
	}

	/*
	 * Make sure this nfsrvboottime is different from all recorded
	 * previous ones.
	 */
	do {
		tryagain = 0;
		for (i = 0; i < sf->nsf_numboots; i++) {
			if (nfsrvboottime == sf->nsf_bootvals[i]) {
				nfsrvboottime++;
				tryagain = 1;
				break;
			}
		}
	} while (tryagain);

	sf->nsf_flags |= NFSNSF_OK;
	off += (sf->nsf_numboots * sizeof (time_t));

	/*
	 * Read through the file, building a list of records for grace
	 * checking.
	 * Each record is between sizeof (struct nfst_rec) and
	 * sizeof (struct nfst_rec) + NFSV4_OPAQUELIMIT - 1
	 * and is actually sizeof (struct nfst_rec) + nst_len - 1.
	 */
	tsp = (struct nfst_rec *)malloc(sizeof (struct nfst_rec) +
		NFSV4_OPAQUELIMIT - 1, M_TEMP, M_WAITOK);
	do {
	    error = NFSD_RDWR(UIO_READ, NFSFPVNODE(sf->nsf_fp),
	        (caddr_t)tsp, sizeof (struct nfst_rec) + NFSV4_OPAQUELIMIT - 1,
	        off, UIO_SYSSPACE, 0, NFSFPCRED(sf->nsf_fp), &aresid, p);
	    len = (sizeof (struct nfst_rec) + NFSV4_OPAQUELIMIT - 1) - aresid;
	    if (error || (len > 0 && (len < sizeof (struct nfst_rec) ||
		len < (sizeof (struct nfst_rec) + tsp->len - 1)))) {
		/*
		 * Yuck, the file has been corrupted, so just return
		 * after clearing out any restart state, so the grace period
		 * is over.
		 */
		LIST_FOREACH_SAFE(sp, &sf->nsf_head, nst_list, nsp) {
			LIST_REMOVE(sp, nst_list);
			free(sp, M_TEMP);
		}
		free(tsp, M_TEMP);
		sf->nsf_flags &= ~NFSNSF_OK;
		free(sf->nsf_bootvals, M_TEMP);
		sf->nsf_bootvals = NULL;
		return;
	    }
	    if (len > 0) {
		off += sizeof (struct nfst_rec) + tsp->len - 1;
		/*
		 * Search the list for a matching client.
		 */
		LIST_FOREACH(sp, &sf->nsf_head, nst_list) {
			if (tsp->len == sp->nst_len &&
			    !NFSBCMP(tsp->client, sp->nst_client, tsp->len))
				break;
		}
		if (sp == LIST_END(&sf->nsf_head)) {
			sp = (struct nfsrv_stable *)malloc(tsp->len +
				sizeof (struct nfsrv_stable) - 1, M_TEMP,
				M_WAITOK);
			NFSBCOPY((caddr_t)tsp, (caddr_t)&sp->nst_rec,
				sizeof (struct nfst_rec) + tsp->len - 1);
			LIST_INSERT_HEAD(&sf->nsf_head, sp, nst_list);
		} else {
			if (tsp->flag == NFSNST_REVOKE)
				sp->nst_flag |= NFSNST_REVOKE;
			else
				/*
				 * A subsequent timestamp indicates the client
				 * did a setclientid/confirm and any previous
				 * revoke is no longer relevant.
				 */
				sp->nst_flag &= ~NFSNST_REVOKE;
		}
	    }
	} while (len > 0);
	free(tsp, M_TEMP);
	sf->nsf_flags = NFSNSF_OK;
	sf->nsf_eograce = NFSD_MONOSEC + sf->nsf_lease +
		NFSRV_LEASEDELTA;
}

/*
 * Update the stable storage file, now that the grace period is over.
 */
APPLESTATIC void
nfsrv_updatestable(NFSPROC_T *p)
{
	struct nfsrv_stablefirst *sf = &nfsrv_stablefirst;
	struct nfsrv_stable *sp, *nsp;
	int i;
	struct nfsvattr nva;
	vnode_t vp;
#if defined(__FreeBSD_version) && (__FreeBSD_version >= 500000)
	mount_t mp = NULL;
#endif
	int error;

	if (sf->nsf_fp == NULL || (sf->nsf_flags & NFSNSF_UPDATEDONE))
		return;
	sf->nsf_flags |= NFSNSF_UPDATEDONE;
	/*
	 * Ok, we need to rewrite the stable storage file.
	 * - truncate to 0 length
	 * - write the new first structure
	 * - loop through the data structures, writing out any that
	 *   have timestamps older than the old boot
	 */
	if (sf->nsf_bootvals) {
		sf->nsf_numboots++;
		for (i = sf->nsf_numboots - 2; i >= 0; i--)
			sf->nsf_bootvals[i + 1] = sf->nsf_bootvals[i];
	} else {
		sf->nsf_numboots = 1;
		sf->nsf_bootvals = (time_t *)malloc(sizeof (time_t),
			M_TEMP, M_WAITOK);
	}
	sf->nsf_bootvals[0] = nfsrvboottime;
	sf->nsf_lease = nfsrv_lease;
	NFSVNO_ATTRINIT(&nva);
	NFSVNO_SETATTRVAL(&nva, size, 0);
	vp = NFSFPVNODE(sf->nsf_fp);
	vn_start_write(vp, &mp, V_WAIT);
	if (NFSVOPLOCK(vp, LK_EXCLUSIVE) == 0) {
		error = nfsvno_setattr(vp, &nva, NFSFPCRED(sf->nsf_fp), p,
		    NULL);
		NFSVOPUNLOCK(vp, 0);
	} else
		error = EPERM;
	vn_finished_write(mp);
	if (!error)
	    error = NFSD_RDWR(UIO_WRITE, vp,
		(caddr_t)&sf->nsf_rec, sizeof (struct nfsf_rec), (off_t)0,
		UIO_SYSSPACE, IO_SYNC, NFSFPCRED(sf->nsf_fp), NULL, p);
	if (!error)
	    error = NFSD_RDWR(UIO_WRITE, vp,
		(caddr_t)sf->nsf_bootvals,
		sf->nsf_numboots * sizeof (time_t),
		(off_t)(sizeof (struct nfsf_rec)),
		UIO_SYSSPACE, IO_SYNC, NFSFPCRED(sf->nsf_fp), NULL, p);
	free(sf->nsf_bootvals, M_TEMP);
	sf->nsf_bootvals = NULL;
	if (error) {
		sf->nsf_flags &= ~NFSNSF_OK;
		printf("EEK! Can't write NfsV4 stable storage file\n");
		return;
	}
	sf->nsf_flags |= NFSNSF_OK;

	/*
	 * Loop through the list and write out timestamp records for
	 * any clients that successfully reclaimed state.
	 */
	LIST_FOREACH_SAFE(sp, &sf->nsf_head, nst_list, nsp) {
		if (sp->nst_flag & NFSNST_GOTSTATE) {
			nfsrv_writestable(sp->nst_client, sp->nst_len,
				NFSNST_NEWSTATE, p);
			sp->nst_clp->lc_flags |= LCL_STAMPEDSTABLE;
		}
		LIST_REMOVE(sp, nst_list);
		free(sp, M_TEMP);
	}
	nfsrv_backupstable();
}

/*
 * Append a record to the stable storage file.
 */
APPLESTATIC void
nfsrv_writestable(u_char *client, int len, int flag, NFSPROC_T *p)
{
	struct nfsrv_stablefirst *sf = &nfsrv_stablefirst;
	struct nfst_rec *sp;
	int error;

	if (!(sf->nsf_flags & NFSNSF_OK) || sf->nsf_fp == NULL)
		return;
	sp = (struct nfst_rec *)malloc(sizeof (struct nfst_rec) +
		len - 1, M_TEMP, M_WAITOK);
	sp->len = len;
	NFSBCOPY(client, sp->client, len);
	sp->flag = flag;
	error = NFSD_RDWR(UIO_WRITE, NFSFPVNODE(sf->nsf_fp),
	    (caddr_t)sp, sizeof (struct nfst_rec) + len - 1, (off_t)0,
	    UIO_SYSSPACE, (IO_SYNC | IO_APPEND), NFSFPCRED(sf->nsf_fp), NULL, p);
	free(sp, M_TEMP);
	if (error) {
		sf->nsf_flags &= ~NFSNSF_OK;
		printf("EEK! Can't write NfsV4 stable storage file\n");
	}
}

/*
 * This function is called during the grace period to mark a client
 * that successfully reclaimed state.
 */
static void
nfsrv_markstable(struct nfsclient *clp)
{
	struct nfsrv_stable *sp;

	/*
	 * First find the client structure.
	 */
	LIST_FOREACH(sp, &nfsrv_stablefirst.nsf_head, nst_list) {
		if (sp->nst_len == clp->lc_idlen &&
		    !NFSBCMP(sp->nst_client, clp->lc_id, sp->nst_len))
			break;
	}
	if (sp == LIST_END(&nfsrv_stablefirst.nsf_head))
		return;

	/*
	 * Now, just mark it and set the nfsclient back pointer.
	 */
	sp->nst_flag |= NFSNST_GOTSTATE;
	sp->nst_clp = clp;
}

/*
 * This function is called when a NFSv4.1 client does a ReclaimComplete.
 * Very similar to nfsrv_markstable(), except for the flag being set.
 */
static void
nfsrv_markreclaim(struct nfsclient *clp)
{
	struct nfsrv_stable *sp;

	/*
	 * First find the client structure.
	 */
	LIST_FOREACH(sp, &nfsrv_stablefirst.nsf_head, nst_list) {
		if (sp->nst_len == clp->lc_idlen &&
		    !NFSBCMP(sp->nst_client, clp->lc_id, sp->nst_len))
			break;
	}
	if (sp == LIST_END(&nfsrv_stablefirst.nsf_head))
		return;

	/*
	 * Now, just set the flag.
	 */
	sp->nst_flag |= NFSNST_RECLAIMED;
}

/*
 * This function is called for a reclaim, to see if it gets grace.
 * It returns 0 if a reclaim is allowed, 1 otherwise.
 */
static int
nfsrv_checkstable(struct nfsclient *clp)
{
	struct nfsrv_stable *sp;

	/*
	 * First, find the entry for the client.
	 */
	LIST_FOREACH(sp, &nfsrv_stablefirst.nsf_head, nst_list) {
		if (sp->nst_len == clp->lc_idlen &&
		    !NFSBCMP(sp->nst_client, clp->lc_id, sp->nst_len))
			break;
	}

	/*
	 * If not in the list, state was revoked or no state was issued
	 * since the previous reboot, a reclaim is denied.
	 */
	if (sp == LIST_END(&nfsrv_stablefirst.nsf_head) ||
	    (sp->nst_flag & NFSNST_REVOKE) ||
	    !(nfsrv_stablefirst.nsf_flags & NFSNSF_OK))
		return (1);
	return (0);
}

/*
 * Test for and try to clear out a conflicting client. This is called by
 * nfsrv_lockctrl() and nfsrv_openctrl() when conflicts with other clients
 * a found.
 * The trick here is that it can't revoke a conflicting client with an
 * expired lease unless it holds the v4root lock, so...
 * If no v4root lock, get the lock and return 1 to indicate "try again".
 * Return 0 to indicate the conflict can't be revoked and 1 to indicate
 * the revocation worked and the conflicting client is "bye, bye", so it
 * can be tried again.
 * Return 2 to indicate that the vnode is VI_DOOMED after NFSVOPLOCK().
 * Unlocks State before a non-zero value is returned.
 */
static int
nfsrv_clientconflict(struct nfsclient *clp, int *haslockp, vnode_t vp,
    NFSPROC_T *p)
{
	int gotlock, lktype = 0;

	/*
	 * If lease hasn't expired, we can't fix it.
	 */
	if (clp->lc_expiry >= NFSD_MONOSEC ||
	    !(nfsrv_stablefirst.nsf_flags & NFSNSF_UPDATEDONE))
		return (0);
	if (*haslockp == 0) {
		NFSUNLOCKSTATE();
		if (vp != NULL) {
			lktype = NFSVOPISLOCKED(vp);
			NFSVOPUNLOCK(vp, 0);
		}
		NFSLOCKV4ROOTMUTEX();
		nfsv4_relref(&nfsv4rootfs_lock);
		do {
			gotlock = nfsv4_lock(&nfsv4rootfs_lock, 1, NULL,
			    NFSV4ROOTLOCKMUTEXPTR, NULL);
		} while (!gotlock);
		NFSUNLOCKV4ROOTMUTEX();
		*haslockp = 1;
		if (vp != NULL) {
			NFSVOPLOCK(vp, lktype | LK_RETRY);
			if ((vp->v_iflag & VI_DOOMED) != 0)
				return (2);
		}
		return (1);
	}
	NFSUNLOCKSTATE();

	/*
	 * Ok, we can expire the conflicting client.
	 */
	nfsrv_writestable(clp->lc_id, clp->lc_idlen, NFSNST_REVOKE, p);
	nfsrv_backupstable();
	nfsrv_cleanclient(clp, p);
	nfsrv_freedeleglist(&clp->lc_deleg);
	nfsrv_freedeleglist(&clp->lc_olddeleg);
	LIST_REMOVE(clp, lc_hash);
	nfsrv_zapclient(clp, p);
	return (1);
}

/*
 * Resolve a delegation conflict.
 * Returns 0 to indicate the conflict was resolved without sleeping.
 * Return -1 to indicate that the caller should check for conflicts again.
 * Return > 0 for an error that should be returned, normally NFSERR_DELAY.
 *
 * Also, manipulate the nfsv4root_lock, as required. It isn't changed
 * for a return of 0, since there was no sleep and it could be required
 * later. It is released for a return of NFSERR_DELAY, since the caller
 * will return that error. It is released when a sleep was done waiting
 * for the delegation to be returned or expire (so that other nfsds can
 * handle ops). Then, it must be acquired for the write to stable storage.
 * (This function is somewhat similar to nfsrv_clientconflict(), but
 *  the semantics differ in a couple of subtle ways. The return of 0
 *  indicates the conflict was resolved without sleeping here, not
 *  that the conflict can't be resolved and the handling of nfsv4root_lock
 *  differs, as noted above.)
 * Unlocks State before returning a non-zero value.
 */
static int
nfsrv_delegconflict(struct nfsstate *stp, int *haslockp, NFSPROC_T *p,
    vnode_t vp)
{
	struct nfsclient *clp = stp->ls_clp;
	int gotlock, error, lktype = 0, retrycnt, zapped_clp;
	nfsv4stateid_t tstateid;
	fhandle_t tfh;

	/*
	 * If the conflict is with an old delegation...
	 */
	if (stp->ls_flags & NFSLCK_OLDDELEG) {
		/*
		 * You can delete it, if it has expired.
		 */
		if (clp->lc_delegtime < NFSD_MONOSEC) {
			nfsrv_freedeleg(stp);
			NFSUNLOCKSTATE();
			error = -1;
			goto out;
		}
		NFSUNLOCKSTATE();
		/*
		 * During this delay, the old delegation could expire or it
		 * could be recovered by the client via an Open with
		 * CLAIM_DELEGATE_PREV.
		 * Release the nfsv4root_lock, if held.
		 */
		if (*haslockp) {
			*haslockp = 0;
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		error = NFSERR_DELAY;
		goto out;
	}

	/*
	 * It's a current delegation, so:
	 * - check to see if the delegation has expired
	 *   - if so, get the v4root lock and then expire it
	 */
	if (!(stp->ls_flags & NFSLCK_DELEGRECALL)) {
		/*
		 * - do a recall callback, since not yet done
		 * For now, never allow truncate to be set. To use
		 * truncate safely, it must be guaranteed that the
		 * Remove, Rename or Setattr with size of 0 will
		 * succeed and that would require major changes to
		 * the VFS/Vnode OPs.
		 * Set the expiry time large enough so that it won't expire
		 * until after the callback, then set it correctly, once
		 * the callback is done. (The delegation will now time
		 * out whether or not the Recall worked ok. The timeout
		 * will be extended when ops are done on the delegation
		 * stateid, up to the timelimit.)
		 */
		stp->ls_delegtime = NFSD_MONOSEC + (2 * nfsrv_lease) +
		    NFSRV_LEASEDELTA;
		stp->ls_delegtimelimit = NFSD_MONOSEC + (6 * nfsrv_lease) +
		    NFSRV_LEASEDELTA;
		stp->ls_flags |= NFSLCK_DELEGRECALL;

		/*
		 * Loop NFSRV_CBRETRYCNT times while the CBRecall replies
		 * NFSERR_BADSTATEID or NFSERR_BADHANDLE. This is done
		 * in order to try and avoid a race that could happen
		 * when a CBRecall request passed the Open reply with
		 * the delegation in it when transitting the network.
		 * Since nfsrv_docallback will sleep, don't use stp after
		 * the call.
		 */
		NFSBCOPY((caddr_t)&stp->ls_stateid, (caddr_t)&tstateid,
		    sizeof (tstateid));
		NFSBCOPY((caddr_t)&stp->ls_lfp->lf_fh, (caddr_t)&tfh,
		    sizeof (tfh));
		NFSUNLOCKSTATE();
		if (*haslockp) {
			*haslockp = 0;
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		retrycnt = 0;
		do {
		    error = nfsrv_docallback(clp, NFSV4OP_CBRECALL,
			&tstateid, 0, &tfh, NULL, NULL, 0, p);
		    retrycnt++;
		} while ((error == NFSERR_BADSTATEID ||
		    error == NFSERR_BADHANDLE) && retrycnt < NFSV4_CBRETRYCNT);
		error = NFSERR_DELAY;
		goto out;
	}

	if (clp->lc_expiry >= NFSD_MONOSEC &&
	    stp->ls_delegtime >= NFSD_MONOSEC) {
		NFSUNLOCKSTATE();
		/*
		 * A recall has been done, but it has not yet expired.
		 * So, RETURN_DELAY.
		 */
		if (*haslockp) {
			*haslockp = 0;
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		error = NFSERR_DELAY;
		goto out;
	}

	/*
	 * If we don't yet have the lock, just get it and then return,
	 * since we need that before deleting expired state, such as
	 * this delegation.
	 * When getting the lock, unlock the vnode, so other nfsds that
	 * are in progress, won't get stuck waiting for the vnode lock.
	 */
	if (*haslockp == 0) {
		NFSUNLOCKSTATE();
		if (vp != NULL) {
			lktype = NFSVOPISLOCKED(vp);
			NFSVOPUNLOCK(vp, 0);
		}
		NFSLOCKV4ROOTMUTEX();
		nfsv4_relref(&nfsv4rootfs_lock);
		do {
			gotlock = nfsv4_lock(&nfsv4rootfs_lock, 1, NULL,
			    NFSV4ROOTLOCKMUTEXPTR, NULL);
		} while (!gotlock);
		NFSUNLOCKV4ROOTMUTEX();
		*haslockp = 1;
		if (vp != NULL) {
			NFSVOPLOCK(vp, lktype | LK_RETRY);
			if ((vp->v_iflag & VI_DOOMED) != 0) {
				*haslockp = 0;
				NFSLOCKV4ROOTMUTEX();
				nfsv4_unlock(&nfsv4rootfs_lock, 1);
				NFSUNLOCKV4ROOTMUTEX();
				error = NFSERR_PERM;
				goto out;
			}
		}
		error = -1;
		goto out;
	}

	NFSUNLOCKSTATE();
	/*
	 * Ok, we can delete the expired delegation.
	 * First, write the Revoke record to stable storage and then
	 * clear out the conflict.
	 * Since all other nfsd threads are now blocked, we can safely
	 * sleep without the state changing.
	 */
	nfsrv_writestable(clp->lc_id, clp->lc_idlen, NFSNST_REVOKE, p);
	nfsrv_backupstable();
	if (clp->lc_expiry < NFSD_MONOSEC) {
		nfsrv_cleanclient(clp, p);
		nfsrv_freedeleglist(&clp->lc_deleg);
		nfsrv_freedeleglist(&clp->lc_olddeleg);
		LIST_REMOVE(clp, lc_hash);
		zapped_clp = 1;
	} else {
		nfsrv_freedeleg(stp);
		zapped_clp = 0;
	}
	if (zapped_clp)
		nfsrv_zapclient(clp, p);
	error = -1;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Check for a remove allowed, if remove is set to 1 and get rid of
 * delegations.
 */
APPLESTATIC int
nfsrv_checkremove(vnode_t vp, int remove, NFSPROC_T *p)
{
	struct nfsstate *stp;
	struct nfslockfile *lfp;
	int error, haslock = 0;
	fhandle_t nfh;

	/*
	 * First, get the lock file structure.
	 * (A return of -1 means no associated state, so remove ok.)
	 */
	error = nfsrv_getlockfh(vp, NFSLCK_CHECK, NULL, &nfh, p);
tryagain:
	NFSLOCKSTATE();
	if (!error)
		error = nfsrv_getlockfile(NFSLCK_CHECK, NULL, &lfp, &nfh, 0);
	if (error) {
		NFSUNLOCKSTATE();
		if (haslock) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		if (error == -1)
			error = 0;
		goto out;
	}

	/*
	 * Now, we must Recall any delegations.
	 */
	error = nfsrv_cleandeleg(vp, lfp, NULL, &haslock, p);
	if (error) {
		/*
		 * nfsrv_cleandeleg() unlocks state for non-zero
		 * return.
		 */
		if (error == -1)
			goto tryagain;
		if (haslock) {
			NFSLOCKV4ROOTMUTEX();
			nfsv4_unlock(&nfsv4rootfs_lock, 1);
			NFSUNLOCKV4ROOTMUTEX();
		}
		goto out;
	}

	/*
	 * Now, look for a conflicting open share.
	 */
	if (remove) {
		/*
		 * If the entry in the directory was the last reference to the
		 * corresponding filesystem object, the object can be destroyed
		 * */
		if(lfp->lf_usecount>1)
			LIST_FOREACH(stp, &lfp->lf_open, ls_file) {
				if (stp->ls_flags & NFSLCK_WRITEDENY) {
					error = NFSERR_FILEOPEN;
					break;
				}
			}
	}

	NFSUNLOCKSTATE();
	if (haslock) {
		NFSLOCKV4ROOTMUTEX();
		nfsv4_unlock(&nfsv4rootfs_lock, 1);
		NFSUNLOCKV4ROOTMUTEX();
	}

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Clear out all delegations for the file referred to by lfp.
 * May return NFSERR_DELAY, if there will be a delay waiting for
 * delegations to expire.
 * Returns -1 to indicate it slept while recalling a delegation.
 * This function has the side effect of deleting the nfslockfile structure,
 * if it no longer has associated state and didn't have to sleep.
 * Unlocks State before a non-zero value is returned.
 */
static int
nfsrv_cleandeleg(vnode_t vp, struct nfslockfile *lfp,
    struct nfsclient *clp, int *haslockp, NFSPROC_T *p)
{
	struct nfsstate *stp, *nstp;
	int ret = 0;

	stp = LIST_FIRST(&lfp->lf_deleg);
	while (stp != LIST_END(&lfp->lf_deleg)) {
		nstp = LIST_NEXT(stp, ls_file);
		if (stp->ls_clp != clp) {
			ret = nfsrv_delegconflict(stp, haslockp, p, vp);
			if (ret) {
				/*
				 * nfsrv_delegconflict() unlocks state
				 * when it returns non-zero.
				 */
				goto out;
			}
		}
		stp = nstp;
	}
out:
	NFSEXITCODE(ret);
	return (ret);
}

/*
 * There are certain operations that, when being done outside of NFSv4,
 * require that any NFSv4 delegation for the file be recalled.
 * This function is to be called for those cases:
 * VOP_RENAME() - When a delegation is being recalled for any reason,
 *	the client may have to do Opens against the server, using the file's
 *	final component name. If the file has been renamed on the server,
 *	that component name will be incorrect and the Open will fail.
 * VOP_REMOVE() - Theoretically, a client could Open a file after it has
 *	been removed on the server, if there is a delegation issued to
 *	that client for the file. I say "theoretically" since clients
 *	normally do an Access Op before the Open and that Access Op will
 *	fail with ESTALE. Note that NFSv2 and 3 don't even do Opens, so
 *	they will detect the file's removal in the same manner. (There is
 *	one case where RFC3530 allows a client to do an Open without first
 *	doing an Access Op, which is passage of a check against the ACE
 *	returned with a Write delegation, but current practice is to ignore
 *	the ACE and always do an Access Op.)
 *	Since the functions can only be called with an unlocked vnode, this
 *	can't be done at this time.
 * VOP_ADVLOCK() - When a client holds a delegation, it can issue byte range
 *	locks locally in the client, which are not visible to the server. To
 *	deal with this, issuing of delegations for a vnode must be disabled
 *	and all delegations for the vnode recalled. This is done via the
 *	second function, using the VV_DISABLEDELEG vflag on the vnode.
 */
APPLESTATIC void
nfsd_recalldelegation(vnode_t vp, NFSPROC_T *p)
{
	time_t starttime;
	int error;

	/*
	 * First, check to see if the server is currently running and it has
	 * been called for a regular file when issuing delegations.
	 */
	if (newnfs_numnfsd == 0 || vp->v_type != VREG ||
	    nfsrv_issuedelegs == 0)
		return;

	KASSERT((NFSVOPISLOCKED(vp) != LK_EXCLUSIVE), ("vp %p is locked", vp));
	/*
	 * First, get a reference on the nfsv4rootfs_lock so that an
	 * exclusive lock cannot be acquired by another thread.
	 */
	NFSLOCKV4ROOTMUTEX();
	nfsv4_getref(&nfsv4rootfs_lock, NULL, NFSV4ROOTLOCKMUTEXPTR, NULL);
	NFSUNLOCKV4ROOTMUTEX();

	/*
	 * Now, call nfsrv_checkremove() in a loop while it returns
	 * NFSERR_DELAY. Return upon any other error or when timed out.
	 */
	starttime = NFSD_MONOSEC;
	do {
		if (NFSVOPLOCK(vp, LK_EXCLUSIVE) == 0) {
			error = nfsrv_checkremove(vp, 0, p);
			NFSVOPUNLOCK(vp, 0);
		} else
			error = EPERM;
		if (error == NFSERR_DELAY) {
			if (NFSD_MONOSEC - starttime > NFS_REMOVETIMEO)
				break;
			/* Sleep for a short period of time */
			(void) nfs_catnap(PZERO, 0, "nfsremove");
		}
	} while (error == NFSERR_DELAY);
	NFSLOCKV4ROOTMUTEX();
	nfsv4_relref(&nfsv4rootfs_lock);
	NFSUNLOCKV4ROOTMUTEX();
}

APPLESTATIC void
nfsd_disabledelegation(vnode_t vp, NFSPROC_T *p)
{

#ifdef VV_DISABLEDELEG
	/*
	 * First, flag issuance of delegations disabled.
	 */
	atomic_set_long(&vp->v_vflag, VV_DISABLEDELEG);
#endif

	/*
	 * Then call nfsd_recalldelegation() to get rid of all extant
	 * delegations.
	 */
	nfsd_recalldelegation(vp, p);
}

/*
 * Check for conflicting locks, etc. and then get rid of delegations.
 * (At one point I thought that I should get rid of delegations for any
 *  Setattr, since it could potentially disallow the I/O op (read or write)
 *  allowed by the delegation. However, Setattr Ops that aren't changing
 *  the size get a stateid of all 0s, so you can't tell if it is a delegation
 *  for the same client or a different one, so I decided to only get rid
 *  of delegations for other clients when the size is being changed.)
 * In general, a Setattr can disable NFS I/O Ops that are outstanding, such
 * as Write backs, even if there is no delegation, so it really isn't any
 * different?)
 */
APPLESTATIC int
nfsrv_checksetattr(vnode_t vp, struct nfsrv_descript *nd,
    nfsv4stateid_t *stateidp, struct nfsvattr *nvap, nfsattrbit_t *attrbitp,
    struct nfsexstuff *exp, NFSPROC_T *p)
{
	struct nfsstate st, *stp = &st;
	struct nfslock lo, *lop = &lo;
	int error = 0;
	nfsquad_t clientid;

	if (NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_SIZE)) {
		stp->ls_flags = (NFSLCK_CHECK | NFSLCK_WRITEACCESS);
		lop->lo_first = nvap->na_size;
	} else {
		stp->ls_flags = 0;
		lop->lo_first = 0;
	}
	if (NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_OWNER) ||
	    NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_OWNERGROUP) ||
	    NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_MODE) ||
	    NFSISSET_ATTRBIT(attrbitp, NFSATTRBIT_ACL))
		stp->ls_flags |= NFSLCK_SETATTR;
	if (stp->ls_flags == 0)
		goto out;
	lop->lo_end = NFS64BITSSET;
	lop->lo_flags = NFSLCK_WRITE;
	stp->ls_ownerlen = 0;
	stp->ls_op = NULL;
	stp->ls_uid = nd->nd_cred->cr_uid;
	stp->ls_stateid.seqid = stateidp->seqid;
	clientid.lval[0] = stp->ls_stateid.other[0] = stateidp->other[0];
	clientid.lval[1] = stp->ls_stateid.other[1] = stateidp->other[1];
	stp->ls_stateid.other[2] = stateidp->other[2];
	error = nfsrv_lockctrl(vp, &stp, &lop, NULL, clientid,
	    stateidp, exp, nd, p);

out:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Check for a write delegation and do a CBGETATTR if there is one, updating
 * the attributes, as required.
 * Should I return an error if I can't get the attributes? (For now, I'll
 * just return ok.
 */
APPLESTATIC int
nfsrv_checkgetattr(struct nfsrv_descript *nd, vnode_t vp,
    struct nfsvattr *nvap, nfsattrbit_t *attrbitp, NFSPROC_T *p)
{
	struct nfsstate *stp;
	struct nfslockfile *lfp;
	struct nfsclient *clp;
	struct nfsvattr nva;
	fhandle_t nfh;
	int error = 0;
	nfsattrbit_t cbbits;
	u_quad_t delegfilerev;

	NFSCBGETATTR_ATTRBIT(attrbitp, &cbbits);
	if (!NFSNONZERO_ATTRBIT(&cbbits))
		goto out;
	if (nfsrv_writedelegcnt == 0)
		goto out;

	/*
	 * Get the lock file structure.
	 * (A return of -1 means no associated state, so return ok.)
	 */
	error = nfsrv_getlockfh(vp, NFSLCK_CHECK, NULL, &nfh, p);
	NFSLOCKSTATE();
	if (!error)
		error = nfsrv_getlockfile(NFSLCK_CHECK, NULL, &lfp, &nfh, 0);
	if (error) {
		NFSUNLOCKSTATE();
		if (error == -1)
			error = 0;
		goto out;
	}

	/*
	 * Now, look for a write delegation.
	 */
	LIST_FOREACH(stp, &lfp->lf_deleg, ls_file) {
		if (stp->ls_flags & NFSLCK_DELEGWRITE)
			break;
	}
	if (stp == LIST_END(&lfp->lf_deleg)) {
		NFSUNLOCKSTATE();
		goto out;
	}
	clp = stp->ls_clp;
	delegfilerev = stp->ls_filerev;

	/*
	 * If the Write delegation was issued as a part of this Compound RPC
	 * or if we have an Implied Clientid (used in a previous Op in this
	 * compound) and it is the client the delegation was issued to,
	 * just return ok.
	 * I also assume that it is from the same client iff the network
	 * host IP address is the same as the callback address. (Not
	 * exactly correct by the RFC, but avoids a lot of Getattr
	 * callbacks.)
	 */
	if (nd->nd_compref == stp->ls_compref ||
	    ((nd->nd_flag & ND_IMPLIEDCLID) &&
	     clp->lc_clientid.qval == nd->nd_clientid.qval) ||
	     nfsaddr2_match(clp->lc_req.nr_nam, nd->nd_nam)) {
		NFSUNLOCKSTATE();
		goto out;
	}

	/*
	 * We are now done with the delegation state structure,
	 * so the statelock can be released and we can now tsleep().
	 */

	/*
	 * Now, we must do the CB Getattr callback, to see if Change or Size
	 * has changed.
	 */
	if (clp->lc_expiry >= NFSD_MONOSEC) {
		NFSUNLOCKSTATE();
		NFSVNO_ATTRINIT(&nva);
		nva.na_filerev = NFS64BITSSET;
		error = nfsrv_docallback(clp, NFSV4OP_CBGETATTR, NULL,
		    0, &nfh, &nva, &cbbits, 0, p);
		if (!error) {
			if ((nva.na_filerev != NFS64BITSSET &&
			    nva.na_filerev > delegfilerev) ||
			    (NFSVNO_ISSETSIZE(&nva) &&
			     nva.na_size != nvap->na_size)) {
				error = nfsvno_updfilerev(vp, nvap, nd, p);
				if (NFSVNO_ISSETSIZE(&nva))
					nvap->na_size = nva.na_size;
			}
		} else
			error = 0;	/* Ignore callback errors for now. */
	} else {
		NFSUNLOCKSTATE();
	}

out:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * This function looks for openowners that haven't had any opens for
 * a while and throws them away. Called by an nfsd when NFSNSF_NOOPENS
 * is set.
 */
APPLESTATIC void
nfsrv_throwawayopens(NFSPROC_T *p)
{
	struct nfsclient *clp, *nclp;
	struct nfsstate *stp, *nstp;
	int i;

	NFSLOCKSTATE();
	nfsrv_stablefirst.nsf_flags &= ~NFSNSF_NOOPENS;
	/*
	 * For each client...
	 */
	for (i = 0; i < nfsrv_clienthashsize; i++) {
	    LIST_FOREACH_SAFE(clp, &nfsclienthash[i], lc_hash, nclp) {
		LIST_FOREACH_SAFE(stp, &clp->lc_open, ls_list, nstp) {
			if (LIST_EMPTY(&stp->ls_open) &&
			    (stp->ls_noopens > NFSNOOPEN ||
			     (nfsrv_openpluslock * 2) >
			     nfsrv_v4statelimit))
				nfsrv_freeopenowner(stp, 0, p);
		}
	    }
	}
	NFSUNLOCKSTATE();
}

/*
 * This function checks to see if the credentials are the same.
 * Returns 1 for not same, 0 otherwise.
 */
static int
nfsrv_notsamecredname(struct nfsrv_descript *nd, struct nfsclient *clp)
{

	if (nd->nd_flag & ND_GSS) {
		if (!(clp->lc_flags & LCL_GSS))
			return (1);
		if (clp->lc_flags & LCL_NAME) {
			if (nd->nd_princlen != clp->lc_namelen ||
			    NFSBCMP(nd->nd_principal, clp->lc_name,
				clp->lc_namelen))
				return (1);
			else
				return (0);
		}
		if (nd->nd_cred->cr_uid == clp->lc_uid)
			return (0);
		else
			return (1);
	} else if (clp->lc_flags & LCL_GSS)
		return (1);
	/*
	 * For AUTH_SYS, allow the same uid or root. (This is underspecified
	 * in RFC3530, which talks about principals, but doesn't say anything
	 * about uids for AUTH_SYS.)
	 */
	if (nd->nd_cred->cr_uid == clp->lc_uid || nd->nd_cred->cr_uid == 0)
		return (0);
	else
		return (1);
}

/*
 * Calculate the lease expiry time.
 */
static time_t
nfsrv_leaseexpiry(void)
{

	if (nfsrv_stablefirst.nsf_eograce > NFSD_MONOSEC)
		return (NFSD_MONOSEC + 2 * (nfsrv_lease + NFSRV_LEASEDELTA));
	return (NFSD_MONOSEC + nfsrv_lease + NFSRV_LEASEDELTA);
}

/*
 * Delay the delegation timeout as far as ls_delegtimelimit, as required.
 */
static void
nfsrv_delaydelegtimeout(struct nfsstate *stp)
{

	if ((stp->ls_flags & NFSLCK_DELEGRECALL) == 0)
		return;

	if ((stp->ls_delegtime + 15) > NFSD_MONOSEC &&
	    stp->ls_delegtime < stp->ls_delegtimelimit) {
		stp->ls_delegtime += nfsrv_lease;
		if (stp->ls_delegtime > stp->ls_delegtimelimit)
			stp->ls_delegtime = stp->ls_delegtimelimit;
	}
}

/*
 * This function checks to see if there is any other state associated
 * with the openowner for this Open.
 * It returns 1 if there is no other state, 0 otherwise.
 */
static int
nfsrv_nootherstate(struct nfsstate *stp)
{
	struct nfsstate *tstp;

	LIST_FOREACH(tstp, &stp->ls_openowner->ls_open, ls_list) {
		if (tstp != stp || !LIST_EMPTY(&tstp->ls_lock))
			return (0);
	}
	return (1);
}

/*
 * Create a list of lock deltas (changes to local byte range locking
 * that can be rolled back using the list) and apply the changes via
 * nfsvno_advlock(). Optionally, lock the list. It is expected that either
 * the rollback or update function will be called after this.
 * It returns an error (and rolls back, as required), if any nfsvno_advlock()
 * call fails. If it returns an error, it will unlock the list.
 */
static int
nfsrv_locallock(vnode_t vp, struct nfslockfile *lfp, int flags,
    uint64_t first, uint64_t end, struct nfslockconflict *cfp, NFSPROC_T *p)
{
	struct nfslock *lop, *nlop;
	int error = 0;

	/* Loop through the list of locks. */
	lop = LIST_FIRST(&lfp->lf_locallock);
	while (first < end && lop != NULL) {
		nlop = LIST_NEXT(lop, lo_lckowner);
		if (first >= lop->lo_end) {
			/* not there yet */
			lop = nlop;
		} else if (first < lop->lo_first) {
			/* new one starts before entry in list */
			if (end <= lop->lo_first) {
				/* no overlap between old and new */
				error = nfsrv_dolocal(vp, lfp, flags,
				    NFSLCK_UNLOCK, first, end, cfp, p);
				if (error != 0)
					break;
				first = end;
			} else {
				/* handle fragment overlapped with new one */
				error = nfsrv_dolocal(vp, lfp, flags,
				    NFSLCK_UNLOCK, first, lop->lo_first, cfp,
				    p);
				if (error != 0)
					break;
				first = lop->lo_first;
			}
		} else {
			/* new one overlaps this entry in list */
			if (end <= lop->lo_end) {
				/* overlaps all of new one */
				error = nfsrv_dolocal(vp, lfp, flags,
				    lop->lo_flags, first, end, cfp, p);
				if (error != 0)
					break;
				first = end;
			} else {
				/* handle fragment overlapped with new one */
				error = nfsrv_dolocal(vp, lfp, flags,
				    lop->lo_flags, first, lop->lo_end, cfp, p);
				if (error != 0)
					break;
				first = lop->lo_end;
				lop = nlop;
			}
		}
	}
	if (first < end && error == 0)
		/* handle fragment past end of list */
		error = nfsrv_dolocal(vp, lfp, flags, NFSLCK_UNLOCK, first,
		    end, cfp, p);

	NFSEXITCODE(error);
	return (error);
}

/*
 * Local lock unlock. Unlock all byte ranges that are no longer locked
 * by NFSv4. To do this, unlock any subranges of first-->end that
 * do not overlap with the byte ranges of any lock in the lfp->lf_lock
 * list. This list has all locks for the file held by other
 * <clientid, lockowner> tuples. The list is ordered by increasing
 * lo_first value, but may have entries that overlap each other, for
 * the case of read locks.
 */
static void
nfsrv_localunlock(vnode_t vp, struct nfslockfile *lfp, uint64_t init_first,
    uint64_t init_end, NFSPROC_T *p)
{
	struct nfslock *lop;
	uint64_t first, end, prevfirst __unused;

	first = init_first;
	end = init_end;
	while (first < init_end) {
		/* Loop through all nfs locks, adjusting first and end */
		prevfirst = 0;
		LIST_FOREACH(lop, &lfp->lf_lock, lo_lckfile) {
			KASSERT(prevfirst <= lop->lo_first,
			    ("nfsv4 locks out of order"));
			KASSERT(lop->lo_first < lop->lo_end,
			    ("nfsv4 bogus lock"));
			prevfirst = lop->lo_first;
			if (first >= lop->lo_first &&
			    first < lop->lo_end)
				/*
				 * Overlaps with initial part, so trim
				 * off that initial part by moving first past
				 * it.
				 */
				first = lop->lo_end;
			else if (end > lop->lo_first &&
			    lop->lo_first > first) {
				/*
				 * This lock defines the end of the
				 * segment to unlock, so set end to the
				 * start of it and break out of the loop.
				 */
				end = lop->lo_first;
				break;
			}
			if (first >= end)
				/*
				 * There is no segment left to do, so
				 * break out of this loop and then exit
				 * the outer while() since first will be set
				 * to end, which must equal init_end here.
				 */
				break;
		}
		if (first < end) {
			/* Unlock this segment */
			(void) nfsrv_dolocal(vp, lfp, NFSLCK_UNLOCK,
			    NFSLCK_READ, first, end, NULL, p);
			nfsrv_locallock_commit(lfp, NFSLCK_UNLOCK,
			    first, end);
		}
		/*
		 * Now move past this segment and look for any further
		 * segment in the range, if there is one.
		 */
		first = end;
		end = init_end;
	}
}

/*
 * Do the local lock operation and update the rollback list, as required.
 * Perform the rollback and return the error if nfsvno_advlock() fails.
 */
static int
nfsrv_dolocal(vnode_t vp, struct nfslockfile *lfp, int flags, int oldflags,
    uint64_t first, uint64_t end, struct nfslockconflict *cfp, NFSPROC_T *p)
{
	struct nfsrollback *rlp;
	int error = 0, ltype, oldltype;

	if (flags & NFSLCK_WRITE)
		ltype = F_WRLCK;
	else if (flags & NFSLCK_READ)
		ltype = F_RDLCK;
	else
		ltype = F_UNLCK;
	if (oldflags & NFSLCK_WRITE)
		oldltype = F_WRLCK;
	else if (oldflags & NFSLCK_READ)
		oldltype = F_RDLCK;
	else
		oldltype = F_UNLCK;
	if (ltype == oldltype || (oldltype == F_WRLCK && ltype == F_RDLCK))
		/* nothing to do */
		goto out;
	error = nfsvno_advlock(vp, ltype, first, end, p);
	if (error != 0) {
		if (cfp != NULL) {
			cfp->cl_clientid.lval[0] = 0;
			cfp->cl_clientid.lval[1] = 0;
			cfp->cl_first = 0;
			cfp->cl_end = NFS64BITSSET;
			cfp->cl_flags = NFSLCK_WRITE;
			cfp->cl_ownerlen = 5;
			NFSBCOPY("LOCAL", cfp->cl_owner, 5);
		}
		nfsrv_locallock_rollback(vp, lfp, p);
	} else if (ltype != F_UNLCK) {
		rlp = malloc(sizeof (struct nfsrollback), M_NFSDROLLBACK,
		    M_WAITOK);
		rlp->rlck_first = first;
		rlp->rlck_end = end;
		rlp->rlck_type = oldltype;
		LIST_INSERT_HEAD(&lfp->lf_rollback, rlp, rlck_list);
	}

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Roll back local lock changes and free up the rollback list.
 */
static void
nfsrv_locallock_rollback(vnode_t vp, struct nfslockfile *lfp, NFSPROC_T *p)
{
	struct nfsrollback *rlp, *nrlp;

	LIST_FOREACH_SAFE(rlp, &lfp->lf_rollback, rlck_list, nrlp) {
		(void) nfsvno_advlock(vp, rlp->rlck_type, rlp->rlck_first,
		    rlp->rlck_end, p);
		free(rlp, M_NFSDROLLBACK);
	}
	LIST_INIT(&lfp->lf_rollback);
}

/*
 * Update local lock list and delete rollback list (ie now committed to the
 * local locks). Most of the work is done by the internal function.
 */
static void
nfsrv_locallock_commit(struct nfslockfile *lfp, int flags, uint64_t first,
    uint64_t end)
{
	struct nfsrollback *rlp, *nrlp;
	struct nfslock *new_lop, *other_lop;

	new_lop = malloc(sizeof (struct nfslock), M_NFSDLOCK, M_WAITOK);
	if (flags & (NFSLCK_READ | NFSLCK_WRITE))
		other_lop = malloc(sizeof (struct nfslock), M_NFSDLOCK,
		    M_WAITOK);
	else
		other_lop = NULL;
	new_lop->lo_flags = flags;
	new_lop->lo_first = first;
	new_lop->lo_end = end;
	nfsrv_updatelock(NULL, &new_lop, &other_lop, lfp);
	if (new_lop != NULL)
		free(new_lop, M_NFSDLOCK);
	if (other_lop != NULL)
		free(other_lop, M_NFSDLOCK);

	/* and get rid of the rollback list */
	LIST_FOREACH_SAFE(rlp, &lfp->lf_rollback, rlck_list, nrlp)
		free(rlp, M_NFSDROLLBACK);
	LIST_INIT(&lfp->lf_rollback);
}

/*
 * Lock the struct nfslockfile for local lock updating.
 */
static void
nfsrv_locklf(struct nfslockfile *lfp)
{
	int gotlock;

	/* lf_usecount ensures *lfp won't be free'd */
	lfp->lf_usecount++;
	do {
		gotlock = nfsv4_lock(&lfp->lf_locallock_lck, 1, NULL,
		    NFSSTATEMUTEXPTR, NULL);
	} while (gotlock == 0);
	lfp->lf_usecount--;
}

/*
 * Unlock the struct nfslockfile after local lock updating.
 */
static void
nfsrv_unlocklf(struct nfslockfile *lfp)
{

	nfsv4_unlock(&lfp->lf_locallock_lck, 0);
}

/*
 * Clear out all state for the NFSv4 server.
 * Must be called by a thread that can sleep when no nfsds are running.
 */
void
nfsrv_throwawayallstate(NFSPROC_T *p)
{
	struct nfsclient *clp, *nclp;
	struct nfslockfile *lfp, *nlfp;
	int i;

	/*
	 * For each client, clean out the state and then free the structure.
	 */
	for (i = 0; i < nfsrv_clienthashsize; i++) {
		LIST_FOREACH_SAFE(clp, &nfsclienthash[i], lc_hash, nclp) {
			nfsrv_cleanclient(clp, p);
			nfsrv_freedeleglist(&clp->lc_deleg);
			nfsrv_freedeleglist(&clp->lc_olddeleg);
			free(clp->lc_stateid, M_NFSDCLIENT);
			free(clp, M_NFSDCLIENT);
		}
	}

	/*
	 * Also, free up any remaining lock file structures.
	 */
	for (i = 0; i < nfsrv_lockhashsize; i++) {
		LIST_FOREACH_SAFE(lfp, &nfslockhash[i], lf_hash, nlfp) {
			printf("nfsd unload: fnd a lock file struct\n");
			nfsrv_freenfslockfile(lfp);
		}
	}

	/* And get rid of the deviceid structures and layouts. */
	nfsrv_freealllayoutsanddevids();
}

/*
 * Check the sequence# for the session and slot provided as an argument.
 * Also, renew the lease if the session will return NFS_OK.
 */
int
nfsrv_checksequence(struct nfsrv_descript *nd, uint32_t sequenceid,
    uint32_t *highest_slotidp, uint32_t *target_highest_slotidp, int cache_this,
    uint32_t *sflagsp, NFSPROC_T *p)
{
	struct nfsdsession *sep;
	struct nfssessionhash *shp;
	int error;
	SVCXPRT *savxprt;

	shp = NFSSESSIONHASH(nd->nd_sessionid);
	NFSLOCKSESSION(shp);
	sep = nfsrv_findsession(nd->nd_sessionid);
	if (sep == NULL) {
		NFSUNLOCKSESSION(shp);
		return (NFSERR_BADSESSION);
	}
	error = nfsv4_seqsession(sequenceid, nd->nd_slotid, *highest_slotidp,
	    sep->sess_slots, NULL, NFSV4_SLOTS - 1);
	if (error != 0) {
		NFSUNLOCKSESSION(shp);
		return (error);
	}
	if (cache_this != 0)
		nd->nd_flag |= ND_SAVEREPLY;
	/* Renew the lease. */
	sep->sess_clp->lc_expiry = nfsrv_leaseexpiry();
	nd->nd_clientid.qval = sep->sess_clp->lc_clientid.qval;
	nd->nd_flag |= ND_IMPLIEDCLID;

	/*
	 * If this session handles the backchannel, save the nd_xprt for this
	 * RPC, since this is the one being used.
	 * RFC-5661 specifies that the fore channel will be implicitly
	 * bound by a Sequence operation.  However, since some NFSv4.1 clients
	 * erroneously assumed that the back channel would be implicitly
	 * bound as well, do the implicit binding unless a
	 * BindConnectiontoSession has already been done on the session.
	 */
	if (sep->sess_clp->lc_req.nr_client != NULL &&
	    sep->sess_cbsess.nfsess_xprt != nd->nd_xprt &&
	    (sep->sess_crflags & NFSV4CRSESS_CONNBACKCHAN) != 0 &&
	    (sep->sess_clp->lc_flags & LCL_DONEBINDCONN) == 0) {
		NFSD_DEBUG(2,
		    "nfsrv_checksequence: implicit back channel bind\n");
		savxprt = sep->sess_cbsess.nfsess_xprt;
		SVC_ACQUIRE(nd->nd_xprt);
		nd->nd_xprt->xp_p2 =
		    sep->sess_clp->lc_req.nr_client->cl_private;
		nd->nd_xprt->xp_idletimeout = 0;	/* Disable timeout. */
		sep->sess_cbsess.nfsess_xprt = nd->nd_xprt;
		if (savxprt != NULL)
			SVC_RELEASE(savxprt);
	}

	*sflagsp = 0;
	if (sep->sess_clp->lc_req.nr_client == NULL)
		*sflagsp |= NFSV4SEQ_CBPATHDOWN;
	NFSUNLOCKSESSION(shp);
	if (error == NFSERR_EXPIRED) {
		*sflagsp |= NFSV4SEQ_EXPIREDALLSTATEREVOKED;
		error = 0;
	} else if (error == NFSERR_ADMINREVOKED) {
		*sflagsp |= NFSV4SEQ_ADMINSTATEREVOKED;
		error = 0;
	}
	*highest_slotidp = *target_highest_slotidp = NFSV4_SLOTS - 1;
	return (0);
}

/*
 * Check/set reclaim complete for this session/clientid.
 */
int
nfsrv_checkreclaimcomplete(struct nfsrv_descript *nd, int onefs)
{
	struct nfsdsession *sep;
	struct nfssessionhash *shp;
	int error = 0;

	shp = NFSSESSIONHASH(nd->nd_sessionid);
	NFSLOCKSTATE();
	NFSLOCKSESSION(shp);
	sep = nfsrv_findsession(nd->nd_sessionid);
	if (sep == NULL) {
		NFSUNLOCKSESSION(shp);
		NFSUNLOCKSTATE();
		return (NFSERR_BADSESSION);
	}

	if (onefs != 0)
		sep->sess_clp->lc_flags |= LCL_RECLAIMONEFS;
		/* Check to see if reclaim complete has already happened. */
	else if ((sep->sess_clp->lc_flags & LCL_RECLAIMCOMPLETE) != 0)
		error = NFSERR_COMPLETEALREADY;
	else {
		sep->sess_clp->lc_flags |= LCL_RECLAIMCOMPLETE;
		nfsrv_markreclaim(sep->sess_clp);
	}
	NFSUNLOCKSESSION(shp);
	NFSUNLOCKSTATE();
	return (error);
}

/*
 * Cache the reply in a session slot.
 */
void
nfsrv_cache_session(uint8_t *sessionid, uint32_t slotid, int repstat,
   struct mbuf **m)
{
	struct nfsdsession *sep;
	struct nfssessionhash *shp;

	shp = NFSSESSIONHASH(sessionid);
	NFSLOCKSESSION(shp);
	sep = nfsrv_findsession(sessionid);
	if (sep == NULL) {
		NFSUNLOCKSESSION(shp);
		printf("nfsrv_cache_session: no session\n");
		m_freem(*m);
		return;
	}
	nfsv4_seqsess_cacherep(slotid, sep->sess_slots, repstat, m);
	NFSUNLOCKSESSION(shp);
}

/*
 * Search for a session that matches the sessionid.
 */
static struct nfsdsession *
nfsrv_findsession(uint8_t *sessionid)
{
	struct nfsdsession *sep;
	struct nfssessionhash *shp;

	shp = NFSSESSIONHASH(sessionid);
	LIST_FOREACH(sep, &shp->list, sess_hash) {
		if (!NFSBCMP(sessionid, sep->sess_sessionid, NFSX_V4SESSIONID))
			break;
	}
	return (sep);
}

/*
 * Destroy a session.
 */
int
nfsrv_destroysession(struct nfsrv_descript *nd, uint8_t *sessionid)
{
	int error, igotlock, samesess;

	samesess = 0;
	if (!NFSBCMP(sessionid, nd->nd_sessionid, NFSX_V4SESSIONID) &&
	    (nd->nd_flag & ND_HASSEQUENCE) != 0) {
		samesess = 1;
		if ((nd->nd_flag & ND_LASTOP) == 0)
			return (NFSERR_BADSESSION);
	}

	/* Lock out other nfsd threads */
	NFSLOCKV4ROOTMUTEX();
	nfsv4_relref(&nfsv4rootfs_lock);
	do {
		igotlock = nfsv4_lock(&nfsv4rootfs_lock, 1, NULL,
		    NFSV4ROOTLOCKMUTEXPTR, NULL);
	} while (igotlock == 0);
	NFSUNLOCKV4ROOTMUTEX();

	error = nfsrv_freesession(NULL, sessionid);
	if (error == 0 && samesess != 0)
		nd->nd_flag &= ~ND_HASSEQUENCE;

	NFSLOCKV4ROOTMUTEX();
	nfsv4_unlock(&nfsv4rootfs_lock, 1);
	NFSUNLOCKV4ROOTMUTEX();
	return (error);
}

/*
 * Bind a connection to a session.
 * For now, only certain variants are supported, since the current session
 * structure can only handle a single backchannel entry, which will be
 * applied to all connections if it is set.
 */
int
nfsrv_bindconnsess(struct nfsrv_descript *nd, uint8_t *sessionid, int *foreaftp)
{
	struct nfssessionhash *shp;
	struct nfsdsession *sep;
	struct nfsclient *clp;
	SVCXPRT *savxprt;
	int error;

	error = 0;
	shp = NFSSESSIONHASH(sessionid);
	NFSLOCKSTATE();
	NFSLOCKSESSION(shp);
	sep = nfsrv_findsession(sessionid);
	if (sep != NULL) {
		clp = sep->sess_clp;
		if (*foreaftp == NFSCDFC4_BACK ||
		    *foreaftp == NFSCDFC4_BACK_OR_BOTH ||
		    *foreaftp == NFSCDFC4_FORE_OR_BOTH) {
			/* Try to set up a backchannel. */
			if (clp->lc_req.nr_client == NULL) {
				NFSD_DEBUG(2, "nfsrv_bindconnsess: acquire "
				    "backchannel\n");
				clp->lc_req.nr_client = (struct __rpc_client *)
				    clnt_bck_create(nd->nd_xprt->xp_socket,
				    sep->sess_cbprogram, NFSV4_CBVERS);
			}
			if (clp->lc_req.nr_client != NULL) {
				NFSD_DEBUG(2, "nfsrv_bindconnsess: set up "
				    "backchannel\n");
				savxprt = sep->sess_cbsess.nfsess_xprt;
				SVC_ACQUIRE(nd->nd_xprt);
				nd->nd_xprt->xp_p2 =
				    clp->lc_req.nr_client->cl_private;
				/* Disable idle timeout. */
				nd->nd_xprt->xp_idletimeout = 0;
				sep->sess_cbsess.nfsess_xprt = nd->nd_xprt;
				if (savxprt != NULL)
					SVC_RELEASE(savxprt);
				sep->sess_crflags |= NFSV4CRSESS_CONNBACKCHAN;
				clp->lc_flags |= LCL_DONEBINDCONN;
				if (*foreaftp == NFSCDFS4_BACK)
					*foreaftp = NFSCDFS4_BACK;
				else
					*foreaftp = NFSCDFS4_BOTH;
			} else if (*foreaftp != NFSCDFC4_BACK) {
				NFSD_DEBUG(2, "nfsrv_bindconnsess: can't set "
				    "up backchannel\n");
				sep->sess_crflags &= ~NFSV4CRSESS_CONNBACKCHAN;
				clp->lc_flags |= LCL_DONEBINDCONN;
				*foreaftp = NFSCDFS4_FORE;
			} else {
				error = NFSERR_NOTSUPP;
				printf("nfsrv_bindconnsess: Can't add "
				    "backchannel\n");
			}
		} else {
			NFSD_DEBUG(2, "nfsrv_bindconnsess: Set forechannel\n");
			clp->lc_flags |= LCL_DONEBINDCONN;
			*foreaftp = NFSCDFS4_FORE;
		}
	} else
		error = NFSERR_BADSESSION;
	NFSUNLOCKSESSION(shp);
	NFSUNLOCKSTATE();
	return (error);
}

/*
 * Free up a session structure.
 */
static int
nfsrv_freesession(struct nfsdsession *sep, uint8_t *sessionid)
{
	struct nfssessionhash *shp;
	int i;

	NFSLOCKSTATE();
	if (sep == NULL) {
		shp = NFSSESSIONHASH(sessionid);
		NFSLOCKSESSION(shp);
		sep = nfsrv_findsession(sessionid);
	} else {
		shp = NFSSESSIONHASH(sep->sess_sessionid);
		NFSLOCKSESSION(shp);
	}
	if (sep != NULL) {
		sep->sess_refcnt--;
		if (sep->sess_refcnt > 0) {
			NFSUNLOCKSESSION(shp);
			NFSUNLOCKSTATE();
			return (NFSERR_BACKCHANBUSY);
		}
		LIST_REMOVE(sep, sess_hash);
		LIST_REMOVE(sep, sess_list);
	}
	NFSUNLOCKSESSION(shp);
	NFSUNLOCKSTATE();
	if (sep == NULL)
		return (NFSERR_BADSESSION);
	for (i = 0; i < NFSV4_SLOTS; i++)
		if (sep->sess_slots[i].nfssl_reply != NULL)
			m_freem(sep->sess_slots[i].nfssl_reply);
	if (sep->sess_cbsess.nfsess_xprt != NULL)
		SVC_RELEASE(sep->sess_cbsess.nfsess_xprt);
	free(sep, M_NFSDSESSION);
	return (0);
}

/*
 * Free a stateid.
 * RFC5661 says that it should fail when there are associated opens, locks
 * or delegations. Since stateids represent opens, I don't see how you can
 * free an open stateid (it will be free'd when closed), so this function
 * only works for lock stateids (freeing the lock_owner) or delegations.
 */
int
nfsrv_freestateid(struct nfsrv_descript *nd, nfsv4stateid_t *stateidp,
    NFSPROC_T *p)
{
	struct nfsclient *clp;
	struct nfsstate *stp;
	int error;

	NFSLOCKSTATE();
	/*
	 * Look up the stateid
	 */
	error = nfsrv_getclient((nfsquad_t)((u_quad_t)0), CLOPS_RENEW, &clp,
	    NULL, (nfsquad_t)((u_quad_t)0), 0, nd, p);
	if (error == 0) {
		/* First, check for a delegation. */
		LIST_FOREACH(stp, &clp->lc_deleg, ls_list) {
			if (!NFSBCMP(stp->ls_stateid.other, stateidp->other,
			    NFSX_STATEIDOTHER))
				break;
		}
		if (stp != NULL) {
			nfsrv_freedeleg(stp);
			NFSUNLOCKSTATE();
			return (error);
		}
	}
	/* Not a delegation, try for a lock_owner. */
	if (error == 0)
		error = nfsrv_getstate(clp, stateidp, 0, &stp);
	if (error == 0 && ((stp->ls_flags & (NFSLCK_OPEN | NFSLCK_DELEGREAD |
	    NFSLCK_DELEGWRITE)) != 0 || (stp->ls_flags & NFSLCK_LOCK) == 0))
		/* Not a lock_owner stateid. */
		error = NFSERR_LOCKSHELD;
	if (error == 0 && !LIST_EMPTY(&stp->ls_lock))
		error = NFSERR_LOCKSHELD;
	if (error == 0)
		nfsrv_freelockowner(stp, NULL, 0, p);
	NFSUNLOCKSTATE();
	return (error);
}

/*
 * Test a stateid.
 */
int
nfsrv_teststateid(struct nfsrv_descript *nd, nfsv4stateid_t *stateidp,
    NFSPROC_T *p)
{
	struct nfsclient *clp;
	struct nfsstate *stp;
	int error;

	NFSLOCKSTATE();
	/*
	 * Look up the stateid
	 */
	error = nfsrv_getclient((nfsquad_t)((u_quad_t)0), CLOPS_RENEW, &clp,
	    NULL, (nfsquad_t)((u_quad_t)0), 0, nd, p);
	if (error == 0)
		error = nfsrv_getstate(clp, stateidp, 0, &stp);
	if (error == 0 && stateidp->seqid != 0 &&
	    SEQ_LT(stateidp->seqid, stp->ls_stateid.seqid))
		error = NFSERR_OLDSTATEID;
	NFSUNLOCKSTATE();
	return (error);
}

/*
 * Generate the xdr for an NFSv4.1 CBSequence Operation.
 */
static int
nfsv4_setcbsequence(struct nfsrv_descript *nd, struct nfsclient *clp,
    int dont_replycache, struct nfsdsession **sepp)
{
	struct nfsdsession *sep;
	uint32_t *tl, slotseq = 0;
	int maxslot, slotpos;
	uint8_t sessionid[NFSX_V4SESSIONID];
	int error;

	error = nfsv4_getcbsession(clp, sepp);
	if (error != 0)
		return (error);
	sep = *sepp;
	(void)nfsv4_sequencelookup(NULL, &sep->sess_cbsess, &slotpos, &maxslot,
	    &slotseq, sessionid);
	KASSERT(maxslot >= 0, ("nfsv4_setcbsequence neg maxslot"));

	/* Build the Sequence arguments. */
	NFSM_BUILD(tl, uint32_t *, NFSX_V4SESSIONID + 5 * NFSX_UNSIGNED);
	bcopy(sessionid, tl, NFSX_V4SESSIONID);
	tl += NFSX_V4SESSIONID / NFSX_UNSIGNED;
	nd->nd_slotseq = tl;
	*tl++ = txdr_unsigned(slotseq);
	*tl++ = txdr_unsigned(slotpos);
	*tl++ = txdr_unsigned(maxslot);
	if (dont_replycache == 0)
		*tl++ = newnfs_true;
	else
		*tl++ = newnfs_false;
	*tl = 0;			/* No referring call list, for now. */
	nd->nd_flag |= ND_HASSEQUENCE;
	return (0);
}

/*
 * Get a session for the callback.
 */
static int
nfsv4_getcbsession(struct nfsclient *clp, struct nfsdsession **sepp)
{
	struct nfsdsession *sep;

	NFSLOCKSTATE();
	LIST_FOREACH(sep, &clp->lc_session, sess_list) {
		if ((sep->sess_crflags & NFSV4CRSESS_CONNBACKCHAN) != 0)
			break;
	}
	if (sep == NULL) {
		NFSUNLOCKSTATE();
		return (NFSERR_BADSESSION);
	}
	sep->sess_refcnt++;
	*sepp = sep;
	NFSUNLOCKSTATE();
	return (0);
}

/*
 * Free up all backchannel xprts.  This needs to be done when the nfsd threads
 * exit, since those transports will all be going away.
 * This is only called after all the nfsd threads are done performing RPCs,
 * so locking shouldn't be an issue.
 */
APPLESTATIC void
nfsrv_freeallbackchannel_xprts(void)
{
	struct nfsdsession *sep;
	struct nfsclient *clp;
	SVCXPRT *xprt;
	int i;

	for (i = 0; i < nfsrv_clienthashsize; i++) {
		LIST_FOREACH(clp, &nfsclienthash[i], lc_hash) {
			LIST_FOREACH(sep, &clp->lc_session, sess_list) {
				xprt = sep->sess_cbsess.nfsess_xprt;
				sep->sess_cbsess.nfsess_xprt = NULL;
				if (xprt != NULL)
					SVC_RELEASE(xprt);
			}
		}
	}
}

/*
 * Do a layout commit.  Actually just call nfsrv_updatemdsattr().
 * I have no idea if the rest of these arguments will ever be useful?
 */
int
nfsrv_layoutcommit(struct nfsrv_descript *nd, vnode_t vp, int layouttype,
    int hasnewoff, uint64_t newoff, uint64_t offset, uint64_t len,
    int hasnewmtime, struct timespec *newmtimep, int reclaim,
    nfsv4stateid_t *stateidp, int maxcnt, char *layp, int *hasnewsizep,
    uint64_t *newsizep, struct ucred *cred, NFSPROC_T *p)
{
	struct nfsvattr na;
	int error;

	error = nfsrv_updatemdsattr(vp, &na, p);
	if (error == 0) {
		*hasnewsizep = 1;
		*newsizep = na.na_size;
	}
	return (error);
}

/*
 * Try and get a layout.
 */
int
nfsrv_layoutget(struct nfsrv_descript *nd, vnode_t vp, struct nfsexstuff *exp,
    int layouttype, int *iomode, uint64_t *offset, uint64_t *len,
    uint64_t minlen, nfsv4stateid_t *stateidp, int maxcnt, int *retonclose,
    int *layoutlenp, char *layp, struct ucred *cred, NFSPROC_T *p)
{
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp;
	char *devid;
	fhandle_t fh, *dsfhp;
	int error, mirrorcnt;

	if (nfsrv_devidcnt == 0)
		return (NFSERR_UNKNLAYOUTTYPE);

	if (*offset != 0)
		printf("nfsrv_layoutget: off=%ju len=%ju\n", (uintmax_t)*offset,
		    (uintmax_t)*len);
	error = nfsvno_getfh(vp, &fh, p);
	NFSD_DEBUG(4, "layoutget getfh=%d\n", error);
	if (error != 0)
		return (error);

	/*
	 * For now, all layouts are for entire files.
	 * Only issue Read/Write layouts if requested for a non-readonly fs.
	 */
	if (NFSVNO_EXRDONLY(exp)) {
		if (*iomode == NFSLAYOUTIOMODE_RW)
			return (NFSERR_LAYOUTTRYLATER);
		*iomode = NFSLAYOUTIOMODE_READ;
	}
	if (*iomode != NFSLAYOUTIOMODE_RW)
		*iomode = NFSLAYOUTIOMODE_READ;

	/*
	 * Check to see if a write layout can be issued for this file.
	 * This is used during mirror recovery to avoid RW layouts being
	 * issued for a file while it is being copied to the recovered
	 * mirror.
	 */
	if (*iomode == NFSLAYOUTIOMODE_RW && nfsrv_dontlayout(&fh) != 0)
		return (NFSERR_LAYOUTTRYLATER);

	*retonclose = 0;
	*offset = 0;
	*len = UINT64_MAX;

	/* First, see if a layout already exists and return if found. */
	lhyp = NFSLAYOUTHASH(&fh);
	NFSLOCKLAYOUT(lhyp);
	error = nfsrv_findlayout(&nd->nd_clientid, &fh, layouttype, p, &lyp);
	NFSD_DEBUG(4, "layoutget findlay=%d\n", error);
	/*
	 * Not sure if the seqid must be the same, so I won't check it.
	 */
	if (error == 0 && (stateidp->other[0] != lyp->lay_stateid.other[0] ||
	    stateidp->other[1] != lyp->lay_stateid.other[1] ||
	    stateidp->other[2] != lyp->lay_stateid.other[2])) {
		if ((lyp->lay_flags & NFSLAY_CALLB) == 0) {
			NFSUNLOCKLAYOUT(lhyp);
			NFSD_DEBUG(1, "ret bad stateid\n");
			return (NFSERR_BADSTATEID);
		}
		/*
		 * I believe we get here because there is a race between
		 * the client processing the CBLAYOUTRECALL and the layout
		 * being deleted here on the server.
		 * The client has now done a LayoutGet with a non-layout
		 * stateid, as it would when there is no layout.
		 * As such, free this layout and set error == NFSERR_BADSTATEID
		 * so the code below will create a new layout structure as
		 * would happen if no layout was found.
		 * "lyp" will be set before being used below, but set it NULL
		 * as a safety belt.
		 */
		nfsrv_freelayout(&lhyp->list, lyp);
		lyp = NULL;
		error = NFSERR_BADSTATEID;
	}
	if (error == 0) {
		if (lyp->lay_layoutlen > maxcnt) {
			NFSUNLOCKLAYOUT(lhyp);
			NFSD_DEBUG(1, "ret layout too small\n");
			return (NFSERR_TOOSMALL);
		}
		if (*iomode == NFSLAYOUTIOMODE_RW)
			lyp->lay_flags |= NFSLAY_RW;
		else
			lyp->lay_flags |= NFSLAY_READ;
		NFSBCOPY(lyp->lay_xdr, layp, lyp->lay_layoutlen);
		*layoutlenp = lyp->lay_layoutlen;
		if (++lyp->lay_stateid.seqid == 0)
			lyp->lay_stateid.seqid = 1;
		stateidp->seqid = lyp->lay_stateid.seqid;
		NFSUNLOCKLAYOUT(lhyp);
		NFSD_DEBUG(4, "ret fnd layout\n");
		return (0);
	}
	NFSUNLOCKLAYOUT(lhyp);

	/* Find the device id and file handle. */
	dsfhp = malloc(sizeof(fhandle_t) * NFSDEV_MAXMIRRORS, M_TEMP, M_WAITOK);
	devid = malloc(NFSX_V4DEVICEID * NFSDEV_MAXMIRRORS, M_TEMP, M_WAITOK);
	error = nfsrv_dsgetdevandfh(vp, p, &mirrorcnt, dsfhp, devid);
	NFSD_DEBUG(4, "layoutget devandfh=%d\n", error);
	if (error == 0) {
		if (layouttype == NFSLAYOUT_NFSV4_1_FILES) {
			if (NFSX_V4FILELAYOUT > maxcnt)
				error = NFSERR_TOOSMALL;
			else
				lyp = nfsrv_filelayout(nd, *iomode, &fh, dsfhp,
				    devid, vp->v_mount->mnt_stat.f_fsid);
		} else {
			if (NFSX_V4FLEXLAYOUT(mirrorcnt) > maxcnt)
				error = NFSERR_TOOSMALL;
			else
				lyp = nfsrv_flexlayout(nd, *iomode, mirrorcnt,
				    &fh, dsfhp, devid,
				    vp->v_mount->mnt_stat.f_fsid);
		}
	}
	free(dsfhp, M_TEMP);
	free(devid, M_TEMP);
	if (error != 0)
		return (error);

	/*
	 * Now, add this layout to the list.
	 */
	error = nfsrv_addlayout(nd, &lyp, stateidp, layp, layoutlenp, p);
	NFSD_DEBUG(4, "layoutget addl=%d\n", error);
	/*
	 * The lyp will be set to NULL by nfsrv_addlayout() if it
	 * linked the new structure into the lists.
	 */
	free(lyp, M_NFSDSTATE);
	return (error);
}

/*
 * Generate a File Layout.
 */
static struct nfslayout *
nfsrv_filelayout(struct nfsrv_descript *nd, int iomode, fhandle_t *fhp,
    fhandle_t *dsfhp, char *devid, fsid_t fs)
{
	uint32_t *tl;
	struct nfslayout *lyp;
	uint64_t pattern_offset;

	lyp = malloc(sizeof(struct nfslayout) + NFSX_V4FILELAYOUT, M_NFSDSTATE,
	    M_WAITOK | M_ZERO);
	lyp->lay_type = NFSLAYOUT_NFSV4_1_FILES;
	if (iomode == NFSLAYOUTIOMODE_RW)
		lyp->lay_flags = NFSLAY_RW;
	else
		lyp->lay_flags = NFSLAY_READ;
	NFSBCOPY(fhp, &lyp->lay_fh, sizeof(*fhp));
	lyp->lay_clientid.qval = nd->nd_clientid.qval;
	lyp->lay_fsid = fs;

	/* Fill in the xdr for the files layout. */
	tl = (uint32_t *)lyp->lay_xdr;
	NFSBCOPY(devid, tl, NFSX_V4DEVICEID);		/* Device ID. */
	tl += (NFSX_V4DEVICEID / NFSX_UNSIGNED);

	/*
	 * Make the stripe size as many 64K blocks as will fit in the stripe
	 * mask. Since there is only one stripe, the stripe size doesn't really
	 * matter, except that the Linux client will only handle an exact
	 * multiple of their PAGE_SIZE (usually 4K).  I chose 64K as a value
	 * that should cover most/all arches w.r.t. PAGE_SIZE.
	 */
	*tl++ = txdr_unsigned(NFSFLAYUTIL_STRIPE_MASK & ~0xffff);
	*tl++ = 0;					/* 1st stripe index. */
	pattern_offset = 0;
	txdr_hyper(pattern_offset, tl); tl += 2;	/* Pattern offset. */
	*tl++ = txdr_unsigned(1);			/* 1 file handle. */
	*tl++ = txdr_unsigned(NFSX_V4PNFSFH);
	NFSBCOPY(dsfhp, tl, sizeof(*dsfhp));
	lyp->lay_layoutlen = NFSX_V4FILELAYOUT;
	return (lyp);
}

#define	FLEX_OWNERID	"999"
#define	FLEX_UID0	"0"
/*
 * Generate a Flex File Layout.
 * The FLEX_OWNERID can be any string of 3 decimal digits. Although this
 * string goes on the wire, it isn't supposed to be used by the client,
 * since this server uses tight coupling.
 * Although not recommended by the spec., if vfs.nfsd.flexlinuxhack=1 use
 * a string of "0". This works around the Linux Flex File Layout driver bug
 * which uses the synthetic uid/gid strings for the "tightly coupled" case.
 */
static struct nfslayout *
nfsrv_flexlayout(struct nfsrv_descript *nd, int iomode, int mirrorcnt,
    fhandle_t *fhp, fhandle_t *dsfhp, char *devid, fsid_t fs)
{
	uint32_t *tl;
	struct nfslayout *lyp;
	uint64_t lenval;
	int i;

	lyp = malloc(sizeof(struct nfslayout) + NFSX_V4FLEXLAYOUT(mirrorcnt),
	    M_NFSDSTATE, M_WAITOK | M_ZERO);
	lyp->lay_type = NFSLAYOUT_FLEXFILE;
	if (iomode == NFSLAYOUTIOMODE_RW)
		lyp->lay_flags = NFSLAY_RW;
	else
		lyp->lay_flags = NFSLAY_READ;
	NFSBCOPY(fhp, &lyp->lay_fh, sizeof(*fhp));
	lyp->lay_clientid.qval = nd->nd_clientid.qval;
	lyp->lay_fsid = fs;
	lyp->lay_mirrorcnt = mirrorcnt;

	/* Fill in the xdr for the files layout. */
	tl = (uint32_t *)lyp->lay_xdr;
	lenval = 0;
	txdr_hyper(lenval, tl); tl += 2;		/* Stripe unit. */
	*tl++ = txdr_unsigned(mirrorcnt);		/* # of mirrors. */
	for (i = 0; i < mirrorcnt; i++) {
		*tl++ = txdr_unsigned(1);		/* One stripe. */
		NFSBCOPY(devid, tl, NFSX_V4DEVICEID);	/* Device ID. */
		tl += (NFSX_V4DEVICEID / NFSX_UNSIGNED);
		devid += NFSX_V4DEVICEID;
		*tl++ = txdr_unsigned(1);		/* Efficiency. */
		*tl++ = 0;				/* Proxy Stateid. */
		*tl++ = 0x55555555;
		*tl++ = 0x55555555;
		*tl++ = 0x55555555;
		*tl++ = txdr_unsigned(1);		/* 1 file handle. */
		*tl++ = txdr_unsigned(NFSX_V4PNFSFH);
		NFSBCOPY(dsfhp, tl, sizeof(*dsfhp));
		tl += (NFSM_RNDUP(NFSX_V4PNFSFH) / NFSX_UNSIGNED);
		dsfhp++;
		if (nfsrv_flexlinuxhack != 0) {
			*tl++ = txdr_unsigned(strlen(FLEX_UID0));
			*tl = 0;		/* 0 pad string. */
			NFSBCOPY(FLEX_UID0, tl++, strlen(FLEX_UID0));
			*tl++ = txdr_unsigned(strlen(FLEX_UID0));
			*tl = 0;		/* 0 pad string. */
			NFSBCOPY(FLEX_UID0, tl++, strlen(FLEX_UID0));
		} else {
			*tl++ = txdr_unsigned(strlen(FLEX_OWNERID));
			NFSBCOPY(FLEX_OWNERID, tl++, NFSX_UNSIGNED);
			*tl++ = txdr_unsigned(strlen(FLEX_OWNERID));
			NFSBCOPY(FLEX_OWNERID, tl++, NFSX_UNSIGNED);
		}
	}
	*tl++ = txdr_unsigned(0);		/* ff_flags. */
	*tl = txdr_unsigned(60);		/* Status interval hint. */
	lyp->lay_layoutlen = NFSX_V4FLEXLAYOUT(mirrorcnt);
	return (lyp);
}

/*
 * Parse and process Flex File errors returned via LayoutReturn.
 */
static void
nfsrv_flexlayouterr(struct nfsrv_descript *nd, uint32_t *layp, int maxcnt,
    NFSPROC_T *p)
{
	uint32_t *tl;
	int cnt, errcnt, i, j, opnum, stat;
	char devid[NFSX_V4DEVICEID];

	tl = layp;
	cnt = fxdr_unsigned(int, *tl++);
	NFSD_DEBUG(4, "flexlayouterr cnt=%d\n", cnt);
	for (i = 0; i < cnt; i++) {
		/* Skip offset, length and stateid for now. */
		tl += (4 + NFSX_STATEID / NFSX_UNSIGNED);
		errcnt = fxdr_unsigned(int, *tl++);
		NFSD_DEBUG(4, "flexlayouterr errcnt=%d\n", errcnt);
		for (j = 0; j < errcnt; j++) {
			NFSBCOPY(tl, devid, NFSX_V4DEVICEID);
			tl += (NFSX_V4DEVICEID / NFSX_UNSIGNED);
			stat = fxdr_unsigned(int, *tl++);
			opnum = fxdr_unsigned(int, *tl++);
			NFSD_DEBUG(4, "flexlayouterr op=%d stat=%d\n", opnum,
			    stat);
			/*
			 * Except for NFSERR_ACCES and NFSERR_STALE errors,
			 * disable the mirror.
			 */
			if (stat != NFSERR_ACCES && stat != NFSERR_STALE)
				nfsrv_delds(devid, p);
		}
	}
}

/*
 * This function removes all flex file layouts which has a mirror with
 * a device id that matches the argument.
 * Called when the DS represented by the device id has failed.
 */
void
nfsrv_flexmirrordel(char *devid, NFSPROC_T *p)
{
	uint32_t *tl;
	struct nfslayout *lyp, *nlyp;
	struct nfslayouthash *lhyp;
	struct nfslayouthead loclyp;
	int i, j;

	NFSD_DEBUG(4, "flexmirrordel\n");
	/* Move all layouts found onto a local list. */
	TAILQ_INIT(&loclyp);
	for (i = 0; i < nfsrv_layouthashsize; i++) {
		lhyp = &nfslayouthash[i];
		NFSLOCKLAYOUT(lhyp);
		TAILQ_FOREACH_SAFE(lyp, &lhyp->list, lay_list, nlyp) {
			if (lyp->lay_type == NFSLAYOUT_FLEXFILE &&
			    lyp->lay_mirrorcnt > 1) {
				NFSD_DEBUG(4, "possible match\n");
				tl = lyp->lay_xdr;
				tl += 3;
				for (j = 0; j < lyp->lay_mirrorcnt; j++) {
					tl++;
					if (NFSBCMP(devid, tl, NFSX_V4DEVICEID)
					    == 0) {
						/* Found one. */
						NFSD_DEBUG(4, "fnd one\n");
						TAILQ_REMOVE(&lhyp->list, lyp,
						    lay_list);
						TAILQ_INSERT_HEAD(&loclyp, lyp,
						    lay_list);
						break;
					}
					tl += (NFSX_V4DEVICEID / NFSX_UNSIGNED +
					    NFSM_RNDUP(NFSX_V4PNFSFH) /
					    NFSX_UNSIGNED + 11 * NFSX_UNSIGNED);
				}
			}
		}
		NFSUNLOCKLAYOUT(lhyp);
	}

	/* Now, try to do a Layout recall for each one found. */
	TAILQ_FOREACH_SAFE(lyp, &loclyp, lay_list, nlyp) {
		NFSD_DEBUG(4, "do layout recall\n");
		/*
		 * The layout stateid.seqid needs to be incremented
		 * before doing a LAYOUT_RECALL callback.
		 */
		if (++lyp->lay_stateid.seqid == 0)
			lyp->lay_stateid.seqid = 1;
		nfsrv_recalllayout(lyp->lay_clientid, &lyp->lay_stateid,
		    &lyp->lay_fh, lyp, 1, lyp->lay_type, p);
		nfsrv_freelayout(&loclyp, lyp);
	}
}

/*
 * Do a recall callback to the client for this layout.
 */
static int
nfsrv_recalllayout(nfsquad_t clid, nfsv4stateid_t *stateidp, fhandle_t *fhp,
    struct nfslayout *lyp, int changed, int laytype, NFSPROC_T *p)
{
	struct nfsclient *clp;
	int error;

	NFSD_DEBUG(4, "nfsrv_recalllayout\n");
	error = nfsrv_getclient(clid, 0, &clp, NULL, (nfsquad_t)((u_quad_t)0),
	    0, NULL, p);
	NFSD_DEBUG(4, "aft nfsrv_getclient=%d\n", error);
	if (error != 0) {
		printf("nfsrv_recalllayout: getclient err=%d\n", error);
		return (error);
	}
	if ((clp->lc_flags & LCL_NFSV41) != 0) {
		error = nfsrv_docallback(clp, NFSV4OP_CBLAYOUTRECALL,
		    stateidp, changed, fhp, NULL, NULL, laytype, p);
		/* If lyp != NULL, handle an error return here. */
		if (error != 0 && lyp != NULL) {
			NFSDRECALLLOCK();
			/*
			 * Mark it returned, since no layout recall
			 * has been done.
			 * All errors seem to be non-recoverable, although
			 * NFSERR_NOMATCHLAYOUT is a normal event.
			 */
			if ((lyp->lay_flags & NFSLAY_RECALL) != 0) {
				lyp->lay_flags |= NFSLAY_RETURNED;
				wakeup(lyp);
			}
			NFSDRECALLUNLOCK();
			if (error != NFSERR_NOMATCHLAYOUT)
				printf("nfsrv_recalllayout: err=%d\n", error);
		}
	} else
		printf("nfsrv_recalllayout: clp not NFSv4.1\n");
	return (error);
}

/*
 * Find a layout to recall when we exceed our high water mark.
 */
void
nfsrv_recalloldlayout(NFSPROC_T *p)
{
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp;
	nfsquad_t clientid;
	nfsv4stateid_t stateid;
	fhandle_t fh;
	int error, laytype, ret;

	lhyp = &nfslayouthash[arc4random() % nfsrv_layouthashsize];
	NFSLOCKLAYOUT(lhyp);
	TAILQ_FOREACH_REVERSE(lyp, &lhyp->list, nfslayouthead, lay_list) {
		if ((lyp->lay_flags & NFSLAY_CALLB) == 0) {
			lyp->lay_flags |= NFSLAY_CALLB;
			/*
			 * The layout stateid.seqid needs to be incremented
			 * before doing a LAYOUT_RECALL callback.
			 */
			if (++lyp->lay_stateid.seqid == 0)
				lyp->lay_stateid.seqid = 1;
			clientid = lyp->lay_clientid;
			stateid = lyp->lay_stateid;
			NFSBCOPY(&lyp->lay_fh, &fh, sizeof(fh));
			laytype = lyp->lay_type;
			break;
		}
	}
	NFSUNLOCKLAYOUT(lhyp);
	if (lyp != NULL) {
		error = nfsrv_recalllayout(clientid, &stateid, &fh, NULL, 0,
		    laytype, p);
		if (error != 0 && error != NFSERR_NOMATCHLAYOUT)
			NFSD_DEBUG(4, "recallold=%d\n", error);
		if (error != 0) {
			NFSLOCKLAYOUT(lhyp);
			/*
			 * Since the hash list was unlocked, we need to
			 * find it again.
			 */
			ret = nfsrv_findlayout(&clientid, &fh, laytype, p,
			    &lyp);
			if (ret == 0 &&
			    (lyp->lay_flags & NFSLAY_CALLB) != 0 &&
			    lyp->lay_stateid.other[0] == stateid.other[0] &&
			    lyp->lay_stateid.other[1] == stateid.other[1] &&
			    lyp->lay_stateid.other[2] == stateid.other[2]) {
				/*
				 * The client no longer knows this layout, so
				 * it can be free'd now.
				 */
				if (error == NFSERR_NOMATCHLAYOUT)
					nfsrv_freelayout(&lhyp->list, lyp);
				else {
					/*
					 * Leave it to be tried later by
					 * clearing NFSLAY_CALLB and moving
					 * it to the head of the list, so it
					 * won't be tried again for a while.
					 */
					lyp->lay_flags &= ~NFSLAY_CALLB;
					TAILQ_REMOVE(&lhyp->list, lyp,
					    lay_list);
					TAILQ_INSERT_HEAD(&lhyp->list, lyp,
					    lay_list);
				}
			}
			NFSUNLOCKLAYOUT(lhyp);
		}
	}
}

/*
 * Try and return layout(s).
 */
int
nfsrv_layoutreturn(struct nfsrv_descript *nd, vnode_t vp,
    int layouttype, int iomode, uint64_t offset, uint64_t len, int reclaim,
    int kind, nfsv4stateid_t *stateidp, int maxcnt, uint32_t *layp, int *fndp,
    struct ucred *cred, NFSPROC_T *p)
{
	struct nfsvattr na;
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp;
	fhandle_t fh;
	int error = 0;

	*fndp = 0;
	if (kind == NFSV4LAYOUTRET_FILE) {
		error = nfsvno_getfh(vp, &fh, p);
		if (error == 0) {
			error = nfsrv_updatemdsattr(vp, &na, p);
			if (error != 0)
				printf("nfsrv_layoutreturn: updatemdsattr"
				    " failed=%d\n", error);
		}
		if (error == 0) {
			if (reclaim == newnfs_true) {
				error = nfsrv_checkgrace(NULL, NULL,
				    NFSLCK_RECLAIM);
				if (error != NFSERR_NOGRACE)
					error = 0;
				return (error);
			}
			lhyp = NFSLAYOUTHASH(&fh);
			NFSDRECALLLOCK();
			NFSLOCKLAYOUT(lhyp);
			error = nfsrv_findlayout(&nd->nd_clientid, &fh,
			    layouttype, p, &lyp);
			NFSD_DEBUG(4, "layoutret findlay=%d\n", error);
			if (error == 0 &&
			    stateidp->other[0] == lyp->lay_stateid.other[0] &&
			    stateidp->other[1] == lyp->lay_stateid.other[1] &&
			    stateidp->other[2] == lyp->lay_stateid.other[2]) {
				NFSD_DEBUG(4, "nfsrv_layoutreturn: stateid %d"
				    " %x %x %x laystateid %d %x %x %x"
				    " off=%ju len=%ju flgs=0x%x\n",
				    stateidp->seqid, stateidp->other[0],
				    stateidp->other[1], stateidp->other[2],
				    lyp->lay_stateid.seqid,
				    lyp->lay_stateid.other[0],
				    lyp->lay_stateid.other[1],
				    lyp->lay_stateid.other[2],
				    (uintmax_t)offset, (uintmax_t)len,
				    lyp->lay_flags);
				if (++lyp->lay_stateid.seqid == 0)
					lyp->lay_stateid.seqid = 1;
				stateidp->seqid = lyp->lay_stateid.seqid;
				if (offset == 0 && len == UINT64_MAX) {
					if ((iomode & NFSLAYOUTIOMODE_READ) !=
					    0)
						lyp->lay_flags &= ~NFSLAY_READ;
					if ((iomode & NFSLAYOUTIOMODE_RW) != 0)
						lyp->lay_flags &= ~NFSLAY_RW;
					if ((lyp->lay_flags & (NFSLAY_READ |
					    NFSLAY_RW)) == 0)
						nfsrv_freelayout(&lhyp->list,
						    lyp);
					else
						*fndp = 1;
				} else
					*fndp = 1;
			}
			NFSUNLOCKLAYOUT(lhyp);
			/* Search the nfsrv_recalllist for a match. */
			TAILQ_FOREACH(lyp, &nfsrv_recalllisthead, lay_list) {
				if (NFSBCMP(&lyp->lay_fh, &fh,
				    sizeof(fh)) == 0 &&
				    lyp->lay_clientid.qval ==
				    nd->nd_clientid.qval &&
				    stateidp->other[0] ==
				    lyp->lay_stateid.other[0] &&
				    stateidp->other[1] ==
				    lyp->lay_stateid.other[1] &&
				    stateidp->other[2] ==
				    lyp->lay_stateid.other[2]) {
					lyp->lay_flags |= NFSLAY_RETURNED;
					wakeup(lyp);
					error = 0;
				}
			}
			NFSDRECALLUNLOCK();
		}
		if (layouttype == NFSLAYOUT_FLEXFILE)
			nfsrv_flexlayouterr(nd, layp, maxcnt, p);
	} else if (kind == NFSV4LAYOUTRET_FSID)
		nfsrv_freelayouts(&nd->nd_clientid,
		    &vp->v_mount->mnt_stat.f_fsid, layouttype, iomode);
	else if (kind == NFSV4LAYOUTRET_ALL)
		nfsrv_freelayouts(&nd->nd_clientid, NULL, layouttype, iomode);
	else
		error = NFSERR_INVAL;
	if (error == -1)
		error = 0;
	return (error);
}

/*
 * Look for an existing layout.
 */
static int
nfsrv_findlayout(nfsquad_t *clientidp, fhandle_t *fhp, int laytype,
    NFSPROC_T *p, struct nfslayout **lypp)
{
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp;
	int ret;

	*lypp = NULL;
	ret = 0;
	lhyp = NFSLAYOUTHASH(fhp);
	TAILQ_FOREACH(lyp, &lhyp->list, lay_list) {
		if (NFSBCMP(&lyp->lay_fh, fhp, sizeof(*fhp)) == 0 &&
		    lyp->lay_clientid.qval == clientidp->qval &&
		    lyp->lay_type == laytype)
			break;
	}
	if (lyp != NULL)
		*lypp = lyp;
	else
		ret = -1;
	return (ret);
}

/*
 * Add the new layout, as required.
 */
static int
nfsrv_addlayout(struct nfsrv_descript *nd, struct nfslayout **lypp,
    nfsv4stateid_t *stateidp, char *layp, int *layoutlenp, NFSPROC_T *p)
{
	struct nfsclient *clp;
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp, *nlyp;
	fhandle_t *fhp;
	int error;

	KASSERT((nd->nd_flag & ND_IMPLIEDCLID) != 0,
	    ("nfsrv_layoutget: no nd_clientid\n"));
	lyp = *lypp;
	fhp = &lyp->lay_fh;
	NFSLOCKSTATE();
	error = nfsrv_getclient((nfsquad_t)((u_quad_t)0), CLOPS_RENEW, &clp,
	    NULL, (nfsquad_t)((u_quad_t)0), 0, nd, p);
	if (error != 0) {
		NFSUNLOCKSTATE();
		return (error);
	}
	lyp->lay_stateid.seqid = stateidp->seqid = 1;
	lyp->lay_stateid.other[0] = stateidp->other[0] =
	    clp->lc_clientid.lval[0];
	lyp->lay_stateid.other[1] = stateidp->other[1] =
	    clp->lc_clientid.lval[1];
	lyp->lay_stateid.other[2] = stateidp->other[2] =
	    nfsrv_nextstateindex(clp);
	NFSUNLOCKSTATE();

	lhyp = NFSLAYOUTHASH(fhp);
	NFSLOCKLAYOUT(lhyp);
	TAILQ_FOREACH(nlyp, &lhyp->list, lay_list) {
		if (NFSBCMP(&nlyp->lay_fh, fhp, sizeof(*fhp)) == 0 &&
		    nlyp->lay_clientid.qval == nd->nd_clientid.qval)
			break;
	}
	if (nlyp != NULL) {
		/* A layout already exists, so use it. */
		nlyp->lay_flags |= (lyp->lay_flags & (NFSLAY_READ | NFSLAY_RW));
		NFSBCOPY(nlyp->lay_xdr, layp, nlyp->lay_layoutlen);
		*layoutlenp = nlyp->lay_layoutlen;
		if (++nlyp->lay_stateid.seqid == 0)
			nlyp->lay_stateid.seqid = 1;
		stateidp->seqid = nlyp->lay_stateid.seqid;
		stateidp->other[0] = nlyp->lay_stateid.other[0];
		stateidp->other[1] = nlyp->lay_stateid.other[1];
		stateidp->other[2] = nlyp->lay_stateid.other[2];
		NFSUNLOCKLAYOUT(lhyp);
		return (0);
	}

	/* Insert the new layout in the lists. */
	*lypp = NULL;
	atomic_add_int(&nfsrv_layoutcnt, 1);
	NFSBCOPY(lyp->lay_xdr, layp, lyp->lay_layoutlen);
	*layoutlenp = lyp->lay_layoutlen;
	TAILQ_INSERT_HEAD(&lhyp->list, lyp, lay_list);
	NFSUNLOCKLAYOUT(lhyp);
	return (0);
}

/*
 * Get the devinfo for a deviceid.
 */
int
nfsrv_getdevinfo(char *devid, int layouttype, uint32_t *maxcnt,
    uint32_t *notify, int *devaddrlen, char **devaddr)
{
	struct nfsdevice *ds;

	if ((layouttype != NFSLAYOUT_NFSV4_1_FILES && layouttype !=
	     NFSLAYOUT_FLEXFILE) ||
	    (nfsrv_maxpnfsmirror > 1 && layouttype == NFSLAYOUT_NFSV4_1_FILES))
		return (NFSERR_UNKNLAYOUTTYPE);

	/*
	 * Now, search for the device id.  Note that the structures won't go
	 * away, but the order changes in the list.  As such, the lock only
	 * needs to be held during the search through the list.
	 */
	NFSDDSLOCK();
	TAILQ_FOREACH(ds, &nfsrv_devidhead, nfsdev_list) {
		if (NFSBCMP(devid, ds->nfsdev_deviceid, NFSX_V4DEVICEID) == 0 &&
		    ds->nfsdev_nmp != NULL)
			break;
	}
	NFSDDSUNLOCK();
	if (ds == NULL)
		return (NFSERR_NOENT);

	/* If the correct nfsdev_XXXXaddrlen is > 0, we have the device info. */
	*devaddrlen = 0;
	if (layouttype == NFSLAYOUT_NFSV4_1_FILES) {
		*devaddrlen = ds->nfsdev_fileaddrlen;
		*devaddr = ds->nfsdev_fileaddr;
	} else if (layouttype == NFSLAYOUT_FLEXFILE) {
		*devaddrlen = ds->nfsdev_flexaddrlen;
		*devaddr = ds->nfsdev_flexaddr;
	}
	if (*devaddrlen == 0)
		return (NFSERR_UNKNLAYOUTTYPE);

	/*
	 * The XDR overhead is 3 unsigned values: layout_type,
	 * length_of_address and notify bitmap.
	 * If the notify array is changed to not all zeros, the
	 * count of unsigned values must be increased.
	 */
	if (*maxcnt > 0 && *maxcnt < NFSM_RNDUP(*devaddrlen) +
	    3 * NFSX_UNSIGNED) {
		*maxcnt = NFSM_RNDUP(*devaddrlen) + 3 * NFSX_UNSIGNED;
		return (NFSERR_TOOSMALL);
	}
	return (0);
}

/*
 * Free a list of layout state structures.
 */
static void
nfsrv_freelayoutlist(nfsquad_t clientid)
{
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp, *nlyp;
	int i;

	for (i = 0; i < nfsrv_layouthashsize; i++) {
		lhyp = &nfslayouthash[i];
		NFSLOCKLAYOUT(lhyp);
		TAILQ_FOREACH_SAFE(lyp, &lhyp->list, lay_list, nlyp) {
			if (lyp->lay_clientid.qval == clientid.qval)
				nfsrv_freelayout(&lhyp->list, lyp);
		}
		NFSUNLOCKLAYOUT(lhyp);
	}
}

/*
 * Free up a layout.
 */
static void
nfsrv_freelayout(struct nfslayouthead *lhp, struct nfslayout *lyp)
{

	NFSD_DEBUG(4, "Freelayout=%p\n", lyp);
	atomic_add_int(&nfsrv_layoutcnt, -1);
	TAILQ_REMOVE(lhp, lyp, lay_list);
	free(lyp, M_NFSDSTATE);
}

/*
 * Free up a device id.
 */
void
nfsrv_freeonedevid(struct nfsdevice *ds)
{
	int i;

	atomic_add_int(&nfsrv_devidcnt, -1);
	vrele(ds->nfsdev_dvp);
	for (i = 0; i < nfsrv_dsdirsize; i++)
		if (ds->nfsdev_dsdir[i] != NULL)
			vrele(ds->nfsdev_dsdir[i]);
	free(ds->nfsdev_fileaddr, M_NFSDSTATE);
	free(ds->nfsdev_flexaddr, M_NFSDSTATE);
	free(ds->nfsdev_host, M_NFSDSTATE);
	free(ds, M_NFSDSTATE);
}

/*
 * Free up a device id and its mirrors.
 */
static void
nfsrv_freedevid(struct nfsdevice *ds)
{

	TAILQ_REMOVE(&nfsrv_devidhead, ds, nfsdev_list);
	nfsrv_freeonedevid(ds);
}

/*
 * Free all layouts and device ids.
 * Done when the nfsd threads are shut down since there may be a new
 * modified device id list created when the nfsd is restarted.
 */
void
nfsrv_freealllayoutsanddevids(void)
{
	struct nfsdontlist *mrp, *nmrp;
	struct nfslayout *lyp, *nlyp;

	/* Get rid of the deviceid structures. */
	nfsrv_freealldevids();
	TAILQ_INIT(&nfsrv_devidhead);
	nfsrv_devidcnt = 0;

	/* Get rid of all layouts. */
	nfsrv_freealllayouts();

	/* Get rid of any nfsdontlist entries. */
	LIST_FOREACH_SAFE(mrp, &nfsrv_dontlisthead, nfsmr_list, nmrp)
		free(mrp, M_NFSDSTATE);
	LIST_INIT(&nfsrv_dontlisthead);
	nfsrv_dontlistlen = 0;

	/* Free layouts in the recall list. */
	TAILQ_FOREACH_SAFE(lyp, &nfsrv_recalllisthead, lay_list, nlyp)
		nfsrv_freelayout(&nfsrv_recalllisthead, lyp);
	TAILQ_INIT(&nfsrv_recalllisthead);
}

/*
 * Free layouts that match the arguments.
 */
static void
nfsrv_freelayouts(nfsquad_t *clid, fsid_t *fs, int laytype, int iomode)
{
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp, *nlyp;
	int i;

	for (i = 0; i < nfsrv_layouthashsize; i++) {
		lhyp = &nfslayouthash[i];
		NFSLOCKLAYOUT(lhyp);
		TAILQ_FOREACH_SAFE(lyp, &lhyp->list, lay_list, nlyp) {
			if (clid->qval != lyp->lay_clientid.qval)
				continue;
			if (fs != NULL && (fs->val[0] != lyp->lay_fsid.val[0] ||
			    fs->val[1] != lyp->lay_fsid.val[1]))
				continue;
			if (laytype != lyp->lay_type)
				continue;
			if ((iomode & NFSLAYOUTIOMODE_READ) != 0)
				lyp->lay_flags &= ~NFSLAY_READ;
			if ((iomode & NFSLAYOUTIOMODE_RW) != 0)
				lyp->lay_flags &= ~NFSLAY_RW;
			if ((lyp->lay_flags & (NFSLAY_READ | NFSLAY_RW)) == 0)
				nfsrv_freelayout(&lhyp->list, lyp);
		}
		NFSUNLOCKLAYOUT(lhyp);
	}
}

/*
 * Free all layouts for the argument file.
 */
void
nfsrv_freefilelayouts(fhandle_t *fhp)
{
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp, *nlyp;

	lhyp = NFSLAYOUTHASH(fhp);
	NFSLOCKLAYOUT(lhyp);
	TAILQ_FOREACH_SAFE(lyp, &lhyp->list, lay_list, nlyp) {
		if (NFSBCMP(&lyp->lay_fh, fhp, sizeof(*fhp)) == 0)
			nfsrv_freelayout(&lhyp->list, lyp);
	}
	NFSUNLOCKLAYOUT(lhyp);
}

/*
 * Free all layouts.
 */
static void
nfsrv_freealllayouts(void)
{
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp, *nlyp;
	int i;

	for (i = 0; i < nfsrv_layouthashsize; i++) {
		lhyp = &nfslayouthash[i];
		NFSLOCKLAYOUT(lhyp);
		TAILQ_FOREACH_SAFE(lyp, &lhyp->list, lay_list, nlyp)
			nfsrv_freelayout(&lhyp->list, lyp);
		NFSUNLOCKLAYOUT(lhyp);
	}
}

/*
 * Look up the mount path for the DS server.
 */
static int
nfsrv_setdsserver(char *dspathp, char *mdspathp, NFSPROC_T *p,
    struct nfsdevice **dsp)
{
	struct nameidata nd;
	struct nfsdevice *ds;
	struct mount *mp;
	int error, i;
	char *dsdirpath;
	size_t dsdirsize;

	NFSD_DEBUG(4, "setdssrv path=%s\n", dspathp);
	*dsp = NULL;
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKSHARED | LOCKLEAF, UIO_SYSSPACE,
	    dspathp, p);
	error = namei(&nd);
	NFSD_DEBUG(4, "lookup=%d\n", error);
	if (error != 0)
		return (error);
	if (nd.ni_vp->v_type != VDIR) {
		vput(nd.ni_vp);
		NFSD_DEBUG(4, "dspath not dir\n");
		return (ENOTDIR);
	}
	if (strcmp(nd.ni_vp->v_mount->mnt_vfc->vfc_name, "nfs") != 0) {
		vput(nd.ni_vp);
		NFSD_DEBUG(4, "dspath not an NFS mount\n");
		return (ENXIO);
	}

	/*
	 * Allocate a DS server structure with the NFS mounted directory
	 * vnode reference counted, so that a non-forced dismount will
	 * fail with EBUSY.
	 * This structure is always linked into the list, even if an error
	 * is being returned.  The caller will free the entire list upon
	 * an error return.
	 */
	*dsp = ds = malloc(sizeof(*ds) + nfsrv_dsdirsize * sizeof(vnode_t),
	    M_NFSDSTATE, M_WAITOK | M_ZERO);
	ds->nfsdev_dvp = nd.ni_vp;
	ds->nfsdev_nmp = VFSTONFS(nd.ni_vp->v_mount);
	NFSVOPUNLOCK(nd.ni_vp, 0);

	dsdirsize = strlen(dspathp) + 16;
	dsdirpath = malloc(dsdirsize, M_TEMP, M_WAITOK);
	/* Now, create the DS directory structures. */
	for (i = 0; i < nfsrv_dsdirsize; i++) {
		snprintf(dsdirpath, dsdirsize, "%s/ds%d", dspathp, i);
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKSHARED | LOCKLEAF,
		    UIO_SYSSPACE, dsdirpath, p);
		error = namei(&nd);
		NFSD_DEBUG(4, "dsdirpath=%s lookup=%d\n", dsdirpath, error);
		if (error != 0)
			break;
		if (nd.ni_vp->v_type != VDIR) {
			vput(nd.ni_vp);
			error = ENOTDIR;
			NFSD_DEBUG(4, "dsdirpath not a VDIR\n");
			break;
		}
		if (strcmp(nd.ni_vp->v_mount->mnt_vfc->vfc_name, "nfs") != 0) {
			vput(nd.ni_vp);
			error = ENXIO;
			NFSD_DEBUG(4, "dsdirpath not an NFS mount\n");
			break;
		}
		ds->nfsdev_dsdir[i] = nd.ni_vp;
		NFSVOPUNLOCK(nd.ni_vp, 0);
	}
	free(dsdirpath, M_TEMP);

	if (strlen(mdspathp) > 0) {
		/*
		 * This DS stores file for a specific MDS exported file
		 * system.
		 */
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKSHARED | LOCKLEAF,
		    UIO_SYSSPACE, mdspathp, p);
		error = namei(&nd);
		NFSD_DEBUG(4, "mds lookup=%d\n", error);
		if (error != 0)
			goto out;
		if (nd.ni_vp->v_type != VDIR) {
			vput(nd.ni_vp);
			error = ENOTDIR;
			NFSD_DEBUG(4, "mdspath not dir\n");
			goto out;
		}
		mp = nd.ni_vp->v_mount;
		if ((mp->mnt_flag & MNT_EXPORTED) == 0) {
			vput(nd.ni_vp);
			error = ENXIO;
			NFSD_DEBUG(4, "mdspath not an exported fs\n");
			goto out;
		}
		ds->nfsdev_mdsfsid = mp->mnt_stat.f_fsid;
		ds->nfsdev_mdsisset = 1;
		vput(nd.ni_vp);
	}

out:
	TAILQ_INSERT_TAIL(&nfsrv_devidhead, ds, nfsdev_list);
	atomic_add_int(&nfsrv_devidcnt, 1);
	return (error);
}

/*
 * Look up the mount path for the DS server and delete it.
 */
int
nfsrv_deldsserver(int op, char *dspathp, NFSPROC_T *p)
{
	struct mount *mp;
	struct nfsmount *nmp;
	struct nfsdevice *ds;
	int error;

	NFSD_DEBUG(4, "deldssrv path=%s\n", dspathp);
	/*
	 * Search for the path in the mount list.  Avoid looking the path
	 * up, since this mount point may be hung, with associated locked
	 * vnodes, etc.
	 * Set NFSMNTP_CANCELRPCS so that any forced dismount will be blocked
	 * until this completes.
	 * As noted in the man page, this should be done before any forced
	 * dismount on the mount point, but at least the handshake on
	 * NFSMNTP_CANCELRPCS should make it safe.
	 */
	error = 0;
	ds = NULL;
	nmp = NULL;
	mtx_lock(&mountlist_mtx);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (strcmp(mp->mnt_stat.f_mntonname, dspathp) == 0 &&
		    strcmp(mp->mnt_stat.f_fstypename, "nfs") == 0 &&
		    mp->mnt_data != NULL) {
			nmp = VFSTONFS(mp);
			NFSLOCKMNT(nmp);
			if ((nmp->nm_privflag & (NFSMNTP_FORCEDISM |
			     NFSMNTP_CANCELRPCS)) == 0) {
				nmp->nm_privflag |= NFSMNTP_CANCELRPCS;
				NFSUNLOCKMNT(nmp);
			} else {
				NFSUNLOCKMNT(nmp);
				nmp = NULL;
			}
			break;
		}
	}
	mtx_unlock(&mountlist_mtx);

	if (nmp != NULL) {
		ds = nfsrv_deldsnmp(op, nmp, p);
		NFSD_DEBUG(4, "deldsnmp=%p\n", ds);
		if (ds != NULL) {
			nfsrv_killrpcs(nmp);
			NFSD_DEBUG(4, "aft killrpcs\n");
		} else
			error = ENXIO;
		NFSLOCKMNT(nmp);
		nmp->nm_privflag &= ~NFSMNTP_CANCELRPCS;
		wakeup(nmp);
		NFSUNLOCKMNT(nmp);
	} else
		error = EINVAL;
	return (error);
}

/*
 * Search for and remove a DS entry which matches the "nmp" argument.
 * The nfsdevice structure pointer is returned so that the caller can
 * free it via nfsrv_freeonedevid().
 * For the forced case, do not try to do LayoutRecalls, since the server
 * must be shut down now anyhow.
 */
struct nfsdevice *
nfsrv_deldsnmp(int op, struct nfsmount *nmp, NFSPROC_T *p)
{
	struct nfsdevice *fndds;

	NFSD_DEBUG(4, "deldsdvp\n");
	NFSDDSLOCK();
	if (op == PNFSDOP_FORCEDELDS)
		fndds = nfsv4_findmirror(nmp);
	else
		fndds = nfsrv_findmirroredds(nmp);
	if (fndds != NULL)
		nfsrv_deleteds(fndds);
	NFSDDSUNLOCK();
	if (fndds != NULL) {
		if (op != PNFSDOP_FORCEDELDS)
			nfsrv_flexmirrordel(fndds->nfsdev_deviceid, p);
		printf("pNFS server: mirror %s failed\n", fndds->nfsdev_host);
	}
	return (fndds);
}

/*
 * Similar to nfsrv_deldsnmp(), except that the DS is indicated by deviceid.
 * This function also calls nfsrv_killrpcs() to unblock RPCs on the mount
 * point.
 * Also, returns an error instead of the nfsdevice found.
 */
static int
nfsrv_delds(char *devid, NFSPROC_T *p)
{
	struct nfsdevice *ds, *fndds;
	struct nfsmount *nmp;
	int fndmirror;

	NFSD_DEBUG(4, "delds\n");
	/*
	 * Search the DS server list for a match with devid.
	 * Remove the DS entry if found and there is a mirror.
	 */
	fndds = NULL;
	nmp = NULL;
	fndmirror = 0;
	NFSDDSLOCK();
	TAILQ_FOREACH(ds, &nfsrv_devidhead, nfsdev_list) {
		if (NFSBCMP(ds->nfsdev_deviceid, devid, NFSX_V4DEVICEID) == 0 &&
		    ds->nfsdev_nmp != NULL) {
			NFSD_DEBUG(4, "fnd main ds\n");
			fndds = ds;
			break;
		}
	}
	if (fndds == NULL) {
		NFSDDSUNLOCK();
		return (ENXIO);
	}
	if (fndds->nfsdev_mdsisset == 0 && nfsrv_faildscnt > 0)
		fndmirror = 1;
	else if (fndds->nfsdev_mdsisset != 0) {
		/* For the fsid is set case, search for a mirror. */
		TAILQ_FOREACH(ds, &nfsrv_devidhead, nfsdev_list) {
			if (ds != fndds && ds->nfsdev_nmp != NULL &&
			    ds->nfsdev_mdsisset != 0 &&
			    ds->nfsdev_mdsfsid.val[0] ==
			    fndds->nfsdev_mdsfsid.val[0] &&
			    ds->nfsdev_mdsfsid.val[1] ==
			    fndds->nfsdev_mdsfsid.val[1]) {
				fndmirror = 1;
				break;
			}
		}
	}
	if (fndmirror != 0) {
		nmp = fndds->nfsdev_nmp;
		NFSLOCKMNT(nmp);
		if ((nmp->nm_privflag & (NFSMNTP_FORCEDISM |
		     NFSMNTP_CANCELRPCS)) == 0) {
			nmp->nm_privflag |= NFSMNTP_CANCELRPCS;
			NFSUNLOCKMNT(nmp);
			nfsrv_deleteds(fndds);
		} else {
			NFSUNLOCKMNT(nmp);
			nmp = NULL;
		}
	}
	NFSDDSUNLOCK();
	if (nmp != NULL) {
		nfsrv_flexmirrordel(fndds->nfsdev_deviceid, p);
		printf("pNFS server: mirror %s failed\n", fndds->nfsdev_host);
		nfsrv_killrpcs(nmp);
		NFSLOCKMNT(nmp);
		nmp->nm_privflag &= ~NFSMNTP_CANCELRPCS;
		wakeup(nmp);
		NFSUNLOCKMNT(nmp);
		return (0);
	}
	return (ENXIO);
}

/*
 * Mark a DS as disabled by setting nfsdev_nmp = NULL.
 */
static void
nfsrv_deleteds(struct nfsdevice *fndds)
{

	NFSD_DEBUG(4, "deleteds: deleting a mirror\n");
	fndds->nfsdev_nmp = NULL;
	if (fndds->nfsdev_mdsisset == 0)
		nfsrv_faildscnt--;
}

/*
 * Fill in the addr structures for the File and Flex File layouts.
 */
static void
nfsrv_allocdevid(struct nfsdevice *ds, char *addr, char *dnshost)
{
	uint32_t *tl;
	char *netprot;
	int addrlen;
	static uint64_t new_devid = 0;

	if (strchr(addr, ':') != NULL)
		netprot = "tcp6";
	else
		netprot = "tcp";

	/* Fill in the device id. */
	NFSBCOPY(&nfsdev_time, ds->nfsdev_deviceid, sizeof(nfsdev_time));
	new_devid++;
	NFSBCOPY(&new_devid, &ds->nfsdev_deviceid[sizeof(nfsdev_time)],
	    sizeof(new_devid));

	/*
	 * Fill in the file addr (actually the nfsv4_file_layout_ds_addr4
	 * as defined in RFC5661) in XDR.
	 */
	addrlen = NFSM_RNDUP(strlen(addr)) + NFSM_RNDUP(strlen(netprot)) +
	    6 * NFSX_UNSIGNED;
	NFSD_DEBUG(4, "hn=%s addr=%s netprot=%s\n", dnshost, addr, netprot);
	ds->nfsdev_fileaddrlen = addrlen;
	tl = malloc(addrlen, M_NFSDSTATE, M_WAITOK | M_ZERO);
	ds->nfsdev_fileaddr = (char *)tl;
	*tl++ = txdr_unsigned(1);		/* One stripe with index 0. */
	*tl++ = 0;
	*tl++ = txdr_unsigned(1);		/* One multipath list */
	*tl++ = txdr_unsigned(1);		/* with one entry in it. */
	/* The netaddr for this one entry. */
	*tl++ = txdr_unsigned(strlen(netprot));
	NFSBCOPY(netprot, tl, strlen(netprot));
	tl += (NFSM_RNDUP(strlen(netprot)) / NFSX_UNSIGNED);
	*tl++ = txdr_unsigned(strlen(addr));
	NFSBCOPY(addr, tl, strlen(addr));

	/*
	 * Fill in the flex file addr (actually the ff_device_addr4
	 * as defined for Flexible File Layout) in XDR.
	 */
	addrlen = NFSM_RNDUP(strlen(addr)) + NFSM_RNDUP(strlen(netprot)) +
	    9 * NFSX_UNSIGNED;
	ds->nfsdev_flexaddrlen = addrlen;
	tl = malloc(addrlen, M_NFSDSTATE, M_WAITOK | M_ZERO);
	ds->nfsdev_flexaddr = (char *)tl;
	*tl++ = txdr_unsigned(1);		/* One multipath entry. */
	/* The netaddr for this one entry. */
	*tl++ = txdr_unsigned(strlen(netprot));
	NFSBCOPY(netprot, tl, strlen(netprot));
	tl += (NFSM_RNDUP(strlen(netprot)) / NFSX_UNSIGNED);
	*tl++ = txdr_unsigned(strlen(addr));
	NFSBCOPY(addr, tl, strlen(addr));
	tl += (NFSM_RNDUP(strlen(addr)) / NFSX_UNSIGNED);
	*tl++ = txdr_unsigned(1);		/* One NFS Version. */
	*tl++ = txdr_unsigned(NFS_VER4);	/* NFSv4. */
	*tl++ = txdr_unsigned(NFSV41_MINORVERSION); /* Minor version 1. */
	*tl++ = txdr_unsigned(NFS_SRVMAXIO);	/* DS max rsize. */
	*tl++ = txdr_unsigned(NFS_SRVMAXIO);	/* DS max wsize. */
	*tl = newnfs_true;			/* Tightly coupled. */

	ds->nfsdev_hostnamelen = strlen(dnshost);
	ds->nfsdev_host = malloc(ds->nfsdev_hostnamelen + 1, M_NFSDSTATE,
	    M_WAITOK);
	NFSBCOPY(dnshost, ds->nfsdev_host, ds->nfsdev_hostnamelen + 1);
}


/*
 * Create the device id list.
 * Return 0 if the nfsd threads are to run and ENXIO if the "-p" argument
 * is misconfigured.
 */
int
nfsrv_createdevids(struct nfsd_nfsd_args *args, NFSPROC_T *p)
{
	struct nfsdevice *ds;
	char *addrp, *dnshostp, *dspathp, *mdspathp;
	int error, i;

	addrp = args->addr;
	dnshostp = args->dnshost;
	dspathp = args->dspath;
	mdspathp = args->mdspath;
	nfsrv_maxpnfsmirror = args->mirrorcnt;
	if (addrp == NULL || dnshostp == NULL || dspathp == NULL ||
	    mdspathp == NULL)
		return (0);

	/*
	 * Loop around for each nul-terminated string in args->addr,
	 * args->dnshost, args->dnspath and args->mdspath.
	 */
	while (addrp < (args->addr + args->addrlen) &&
	    dnshostp < (args->dnshost + args->dnshostlen) &&
	    dspathp < (args->dspath + args->dspathlen) &&
	    mdspathp < (args->mdspath + args->mdspathlen)) {
		error = nfsrv_setdsserver(dspathp, mdspathp, p, &ds);
		if (error != 0) {
			/* Free all DS servers. */
			nfsrv_freealldevids();
			nfsrv_devidcnt = 0;
			return (ENXIO);
		}
		nfsrv_allocdevid(ds, addrp, dnshostp);
		addrp += (strlen(addrp) + 1);
		dnshostp += (strlen(dnshostp) + 1);
		dspathp += (strlen(dspathp) + 1);
		mdspathp += (strlen(mdspathp) + 1);
	}
	if (nfsrv_devidcnt < nfsrv_maxpnfsmirror) {
		/* Free all DS servers. */
		nfsrv_freealldevids();
		nfsrv_devidcnt = 0;
		nfsrv_maxpnfsmirror = 1;
		return (ENXIO);
	}
	/* We can fail at most one less DS than the mirror level. */
	nfsrv_faildscnt = nfsrv_maxpnfsmirror - 1;

	/*
	 * Allocate the nfslayout hash table now, since this is a pNFS server.
	 * Make it 1% of the high water mark and at least 100.
	 */
	if (nfslayouthash == NULL) {
		nfsrv_layouthashsize = nfsrv_layouthighwater / 100;
		if (nfsrv_layouthashsize < 100)
			nfsrv_layouthashsize = 100;
		nfslayouthash = mallocarray(nfsrv_layouthashsize,
		    sizeof(struct nfslayouthash), M_NFSDSESSION, M_WAITOK |
		    M_ZERO);
		for (i = 0; i < nfsrv_layouthashsize; i++) {
			mtx_init(&nfslayouthash[i].mtx, "nfslm", NULL, MTX_DEF);
			TAILQ_INIT(&nfslayouthash[i].list);
		}
	}
	return (0);
}

/*
 * Free all device ids.
 */
static void
nfsrv_freealldevids(void)
{
	struct nfsdevice *ds, *nds;

	TAILQ_FOREACH_SAFE(ds, &nfsrv_devidhead, nfsdev_list, nds)
		nfsrv_freedevid(ds);
}

/*
 * Check to see if there is a Read/Write Layout plus either:
 * - A Write Delegation
 * or
 * - An Open with Write_access.
 * Return 1 if this is the case and 0 otherwise.
 * This function is used by nfsrv_proxyds() to decide if doing a Proxy
 * Getattr RPC to the Data Server (DS) is necessary.
 */
#define	NFSCLIDVECSIZE	6
APPLESTATIC int
nfsrv_checkdsattr(struct nfsrv_descript *nd, vnode_t vp, NFSPROC_T *p)
{
	fhandle_t fh, *tfhp;
	struct nfsstate *stp;
	struct nfslayout *lyp;
	struct nfslayouthash *lhyp;
	struct nfslockhashhead *hp;
	struct nfslockfile *lfp;
	nfsquad_t clid[NFSCLIDVECSIZE];
	int clidcnt, ret;

	ret = nfsvno_getfh(vp, &fh, p);
	if (ret != 0)
		return (0);

	/* First check for a Read/Write Layout. */
	clidcnt = 0;
	lhyp = NFSLAYOUTHASH(&fh);
	NFSLOCKLAYOUT(lhyp);
	TAILQ_FOREACH(lyp, &lhyp->list, lay_list) {
		if (NFSBCMP(&lyp->lay_fh, &fh, sizeof(fh)) == 0 &&
		    ((lyp->lay_flags & NFSLAY_RW) != 0 ||
		     ((lyp->lay_flags & NFSLAY_READ) != 0 &&
		      nfsrv_pnfsatime != 0))) {
			if (clidcnt < NFSCLIDVECSIZE)
				clid[clidcnt].qval = lyp->lay_clientid.qval;
			clidcnt++;
		}
	}
	NFSUNLOCKLAYOUT(lhyp);
	if (clidcnt == 0) {
		/* None found, so return 0. */
		return (0);
	}

	/* Get the nfslockfile for this fh. */
	NFSLOCKSTATE();
	hp = NFSLOCKHASH(&fh);
	LIST_FOREACH(lfp, hp, lf_hash) {
		tfhp = &lfp->lf_fh;
		if (NFSVNO_CMPFH(&fh, tfhp))
			break;
	}
	if (lfp == NULL) {
		/* None found, so return 0. */
		NFSUNLOCKSTATE();
		return (0);
	}

	/* Now, look for a Write delegation for this clientid. */
	LIST_FOREACH(stp, &lfp->lf_deleg, ls_file) {
		if ((stp->ls_flags & NFSLCK_DELEGWRITE) != 0 &&
		    nfsrv_fndclid(clid, stp->ls_clp->lc_clientid, clidcnt) != 0)
			break;
	}
	if (stp != NULL) {
		/* Found one, so return 1. */
		NFSUNLOCKSTATE();
		return (1);
	}

	/* No Write delegation, so look for an Open with Write_access. */
	LIST_FOREACH(stp, &lfp->lf_open, ls_file) {
		KASSERT((stp->ls_flags & NFSLCK_OPEN) != 0,
		    ("nfsrv_checkdsattr: Non-open in Open list\n"));
		if ((stp->ls_flags & NFSLCK_WRITEACCESS) != 0 &&
		    nfsrv_fndclid(clid, stp->ls_clp->lc_clientid, clidcnt) != 0)
			break;
	}
	NFSUNLOCKSTATE();
	if (stp != NULL)
		return (1);
	return (0);
}

/*
 * Look for a matching clientid in the vector. Return 1 if one might match.
 */
static int
nfsrv_fndclid(nfsquad_t *clidvec, nfsquad_t clid, int clidcnt)
{
	int i;

	/* If too many for the vector, return 1 since there might be a match. */
	if (clidcnt > NFSCLIDVECSIZE)
		return (1);

	for (i = 0; i < clidcnt; i++)
		if (clidvec[i].qval == clid.qval)
			return (1);
	return (0);
}

/*
 * Check the don't list for "vp" and see if issuing an rw layout is allowed.
 * Return 1 if issuing an rw layout isn't allowed, 0 otherwise.
 */
static int
nfsrv_dontlayout(fhandle_t *fhp)
{
	struct nfsdontlist *mrp;
	int ret;

	if (nfsrv_dontlistlen == 0)
		return (0);
	ret = 0;
	NFSDDONTLISTLOCK();
	LIST_FOREACH(mrp, &nfsrv_dontlisthead, nfsmr_list) {
		if (NFSBCMP(fhp, &mrp->nfsmr_fh, sizeof(*fhp)) == 0 &&
		    (mrp->nfsmr_flags & NFSMR_DONTLAYOUT) != 0) {
			ret = 1;
			break;
		}
	}
	NFSDDONTLISTUNLOCK();
	return (ret);
}

#define	PNFSDS_COPYSIZ	65536
/*
 * Create a new file on a DS and copy the contents of an extant DS file to it.
 * This can be used for recovery of a DS file onto a recovered DS.
 * The steps are:
 * - When called, the MDS file's vnode is locked, blocking LayoutGet operations.
 * - Disable issuing of read/write layouts for the file via the nfsdontlist,
 *   so that they will be disabled after the MDS file's vnode is unlocked.
 * - Set up the nfsrv_recalllist so that recall of read/write layouts can
 *   be done.
 * - Unlock the MDS file's vnode, so that the client(s) can perform proxied
 *   writes, LayoutCommits and LayoutReturns for the file when completing the
 *   LayoutReturn requested by the LayoutRecall callback.
 * - Issue a LayoutRecall callback for all read/write layouts and wait for
 *   them to be returned. (If the LayoutRecall callback replies
 *   NFSERR_NOMATCHLAYOUT, they are gone and no LayoutReturn is needed.)
 * - Exclusively lock the MDS file's vnode.  This ensures that no proxied
 *   writes are in progress or can occur during the DS file copy.
 *   It also blocks Setattr operations.
 * - Create the file on the recovered mirror.
 * - Copy the file from the operational DS.
 * - Copy any ACL from the MDS file to the new DS file.
 * - Set the modify time of the new DS file to that of the MDS file.
 * - Update the extended attribute for the MDS file.
 * - Enable issuing of rw layouts by deleting the nfsdontlist entry.
 * - The caller will unlock the MDS file's vnode allowing operations
 *   to continue normally, since it is now on the mirror again.
 */
int
nfsrv_copymr(vnode_t vp, vnode_t fvp, vnode_t dvp, struct nfsdevice *ds,
    struct pnfsdsfile *pf, struct pnfsdsfile *wpf, int mirrorcnt,
    struct ucred *cred, NFSPROC_T *p)
{
	struct nfsdontlist *mrp, *nmrp;
	struct nfslayouthash *lhyp;
	struct nfslayout *lyp, *nlyp;
	struct nfslayouthead thl;
	struct mount *mp, *tvmp;
	struct acl *aclp;
	struct vattr va;
	struct timespec mtime;
	fhandle_t fh;
	vnode_t tvp;
	off_t rdpos, wrpos;
	ssize_t aresid;
	char *dat;
	int didprintf, ret, retacl, xfer;

	ASSERT_VOP_LOCKED(fvp, "nfsrv_copymr fvp");
	ASSERT_VOP_LOCKED(vp, "nfsrv_copymr vp");
	/*
	 * Allocate a nfsdontlist entry and set the NFSMR_DONTLAYOUT flag
	 * so that no more RW layouts will get issued.
	 */
	ret = nfsvno_getfh(vp, &fh, p);
	if (ret != 0) {
		NFSD_DEBUG(4, "nfsrv_copymr: getfh=%d\n", ret);
		return (ret);
	}
	nmrp = malloc(sizeof(*nmrp), M_NFSDSTATE, M_WAITOK);
	nmrp->nfsmr_flags = NFSMR_DONTLAYOUT;
	NFSBCOPY(&fh, &nmrp->nfsmr_fh, sizeof(fh));
	NFSDDONTLISTLOCK();
	LIST_FOREACH(mrp, &nfsrv_dontlisthead, nfsmr_list) {
		if (NFSBCMP(&fh, &mrp->nfsmr_fh, sizeof(fh)) == 0)
			break;
	}
	if (mrp == NULL) {
		LIST_INSERT_HEAD(&nfsrv_dontlisthead, nmrp, nfsmr_list);
		mrp = nmrp;
		nmrp = NULL;
		nfsrv_dontlistlen++;
		NFSD_DEBUG(4, "nfsrv_copymr: in dontlist\n");
	} else {
		NFSDDONTLISTUNLOCK();
		free(nmrp, M_NFSDSTATE);
		NFSD_DEBUG(4, "nfsrv_copymr: dup dontlist\n");
		return (ENXIO);
	}
	NFSDDONTLISTUNLOCK();

	/*
	 * Search for all RW layouts for this file.  Move them to the
	 * recall list, so they can be recalled and their return noted.
	 */
	lhyp = NFSLAYOUTHASH(&fh);
	NFSDRECALLLOCK();
	NFSLOCKLAYOUT(lhyp);
	TAILQ_FOREACH_SAFE(lyp, &lhyp->list, lay_list, nlyp) {
		if (NFSBCMP(&lyp->lay_fh, &fh, sizeof(fh)) == 0 &&
		    (lyp->lay_flags & NFSLAY_RW) != 0) {
			TAILQ_REMOVE(&lhyp->list, lyp, lay_list);
			TAILQ_INSERT_HEAD(&nfsrv_recalllisthead, lyp, lay_list);
			lyp->lay_trycnt = 0;
		}
	}
	NFSUNLOCKLAYOUT(lhyp);
	NFSDRECALLUNLOCK();

	ret = 0;
	mp = tvmp = NULL;
	didprintf = 0;
	TAILQ_INIT(&thl);
	/* Unlock the MDS vp, so that a LayoutReturn can be done on it. */
	NFSVOPUNLOCK(vp, 0);
	/* Now, do a recall for all layouts not yet recalled. */
tryagain:
	NFSDRECALLLOCK();
	TAILQ_FOREACH(lyp, &nfsrv_recalllisthead, lay_list) {
		if (NFSBCMP(&lyp->lay_fh, &fh, sizeof(fh)) == 0 &&
		    (lyp->lay_flags & NFSLAY_RECALL) == 0) {
			lyp->lay_flags |= NFSLAY_RECALL;
			/*
			 * The layout stateid.seqid needs to be incremented
			 * before doing a LAYOUT_RECALL callback.
			 */
			if (++lyp->lay_stateid.seqid == 0)
				lyp->lay_stateid.seqid = 1;
			NFSDRECALLUNLOCK();
			nfsrv_recalllayout(lyp->lay_clientid, &lyp->lay_stateid,
			    &lyp->lay_fh, lyp, 0, lyp->lay_type, p);
			NFSD_DEBUG(4, "nfsrv_copymr: recalled layout\n");
			goto tryagain;
		}
	}

	/* Now wait for them to be returned. */
tryagain2:
	TAILQ_FOREACH(lyp, &nfsrv_recalllisthead, lay_list) {
		if (NFSBCMP(&lyp->lay_fh, &fh, sizeof(fh)) == 0) {
			if ((lyp->lay_flags & NFSLAY_RETURNED) != 0) {
				TAILQ_REMOVE(&nfsrv_recalllisthead, lyp,
				    lay_list);
				TAILQ_INSERT_HEAD(&thl, lyp, lay_list);
				NFSD_DEBUG(4,
				    "nfsrv_copymr: layout returned\n");
			} else {
				lyp->lay_trycnt++;
				ret = mtx_sleep(lyp, NFSDRECALLMUTEXPTR,
				    PVFS | PCATCH, "nfsmrl", hz);
				NFSD_DEBUG(4, "nfsrv_copymr: aft sleep=%d\n",
				    ret);
				if (ret == EINTR || ret == ERESTART)
					break;
				if ((lyp->lay_flags & NFSLAY_RETURNED) == 0) {
					/*
					 * Give up after 60sec and return
					 * ENXIO, failing the copymr.
					 * This layout will remain on the
					 * recalllist.  It can only be cleared
					 * by restarting the nfsd.
					 * This seems the safe way to handle
					 * it, since it cannot be safely copied
					 * with an outstanding RW layout.
					 */
					if (lyp->lay_trycnt >= 60) {
						ret = ENXIO;
						break;
					}
					if (didprintf == 0) {
						printf("nfsrv_copymr: layout "
						    "not returned\n");
						didprintf = 1;
					}
				}
			}
			goto tryagain2;
		}
	}
	NFSDRECALLUNLOCK();
	/* We can now get rid of the layouts that have been returned. */
	TAILQ_FOREACH_SAFE(lyp, &thl, lay_list, nlyp)
		nfsrv_freelayout(&thl, lyp);

	/*
	 * Do the vn_start_write() calls here, before the MDS vnode is
	 * locked and the tvp is created (locked) in the NFS file system
	 * that dvp is in.
	 * For tvmp, this probably isn't necessary, since it will be an
	 * NFS mount and they are not suspendable at this time.
	 */
	if (ret == 0)
		ret = vn_start_write(vp, &mp, V_WAIT | PCATCH);
	if (ret == 0) {
		tvmp = dvp->v_mount;
		ret = vn_start_write(NULL, &tvmp, V_WAIT | PCATCH);
	}

	/*
	 * LK_EXCLUSIVE lock the MDS vnode, so that any
	 * proxied writes through the MDS will be blocked until we have
	 * completed the copy and update of the extended attributes.
	 * This will also ensure that any attributes and ACL will not be
	 * changed until the copy is complete.
	 */
	NFSVOPLOCK(vp, LK_EXCLUSIVE | LK_RETRY);
	if (ret == 0 && (vp->v_iflag & VI_DOOMED) != 0) {
		NFSD_DEBUG(4, "nfsrv_copymr: lk_exclusive doomed\n");
		ret = ESTALE;
	}

	/* Create the data file on the recovered DS. */
	if (ret == 0)
		ret = nfsrv_createdsfile(vp, &fh, pf, dvp, ds, cred, p, &tvp);

	/* Copy the DS file, if created successfully. */
	if (ret == 0) {
		/*
		 * Get any NFSv4 ACL on the MDS file, so that it can be set
		 * on the new DS file.
		 */
		aclp = acl_alloc(M_WAITOK | M_ZERO);
		retacl = VOP_GETACL(vp, ACL_TYPE_NFS4, aclp, cred, p);
		if (retacl != 0 && retacl != ENOATTR)
			NFSD_DEBUG(1, "nfsrv_copymr: vop_getacl=%d\n", retacl);
		dat = malloc(PNFSDS_COPYSIZ, M_TEMP, M_WAITOK);
		/* Malloc a block of 0s used to check for holes. */
		if (nfsrv_zeropnfsdat == NULL)
			nfsrv_zeropnfsdat = malloc(PNFSDS_COPYSIZ, M_TEMP,
			    M_WAITOK | M_ZERO);
		rdpos = wrpos = 0;
		ret = VOP_GETATTR(fvp, &va, cred);
		aresid = 0;
		while (ret == 0 && aresid == 0) {
			ret = vn_rdwr(UIO_READ, fvp, dat, PNFSDS_COPYSIZ,
			    rdpos, UIO_SYSSPACE, IO_NODELOCKED, cred, NULL,
			    &aresid, p);
			xfer = PNFSDS_COPYSIZ - aresid;
			if (ret == 0 && xfer > 0) {
				rdpos += xfer;
				/*
				 * Skip the write for holes, except for the
				 * last block.
				 */
				if (xfer < PNFSDS_COPYSIZ || rdpos ==
				    va.va_size || NFSBCMP(dat,
				    nfsrv_zeropnfsdat, PNFSDS_COPYSIZ) != 0)
					ret = vn_rdwr(UIO_WRITE, tvp, dat, xfer,
					    wrpos, UIO_SYSSPACE, IO_NODELOCKED,
					    cred, NULL, NULL, p);
				if (ret == 0)
					wrpos += xfer;
			}
		}

		/* If there is an ACL and the copy succeeded, set the ACL. */
		if (ret == 0 && retacl == 0) {
			ret = VOP_SETACL(tvp, ACL_TYPE_NFS4, aclp, cred, p);
			/*
			 * Don't consider these as errors, since VOP_GETACL()
			 * can return an ACL when they are not actually
			 * supported.  For example, for UFS, VOP_GETACL()
			 * will return a trivial ACL based on the uid/gid/mode
			 * when there is no ACL on the file.
			 * This case should be recognized as a trivial ACL
			 * by UFS's VOP_SETACL() and succeed, but...
			 */
			if (ret == ENOATTR || ret == EOPNOTSUPP || ret == EPERM)
				ret = 0;
		}

		if (ret == 0)
			ret = VOP_FSYNC(tvp, MNT_WAIT, p);

		/* Set the DS data file's modify time that of the MDS file. */
		if (ret == 0)
			ret = VOP_GETATTR(vp, &va, cred);
		if (ret == 0) {
			mtime = va.va_mtime;
			VATTR_NULL(&va);
			va.va_mtime = mtime;
			ret = VOP_SETATTR(tvp, &va, cred);
		}

		vput(tvp);
		acl_free(aclp);
		free(dat, M_TEMP);
	}
	if (tvmp != NULL)
		vn_finished_write(tvmp);

	/* Update the extended attributes for the newly created DS file. */
	if (ret == 0)
		ret = vn_extattr_set(vp, IO_NODELOCKED,
		    EXTATTR_NAMESPACE_SYSTEM, "pnfsd.dsfile",
		    sizeof(*wpf) * mirrorcnt, (char *)wpf, p);
	if (mp != NULL)
		vn_finished_write(mp);

	/* Get rid of the dontlist entry, so that Layouts can be issued. */
	NFSDDONTLISTLOCK();
	LIST_REMOVE(mrp, nfsmr_list);
	NFSDDONTLISTUNLOCK();
	free(mrp, M_NFSDSTATE);
	return (ret);
}

/*
 * Create a data storage file on the recovered DS.
 */
static int
nfsrv_createdsfile(vnode_t vp, fhandle_t *fhp, struct pnfsdsfile *pf,
    vnode_t dvp, struct nfsdevice *ds, struct ucred *cred, NFSPROC_T *p,
    vnode_t *tvpp)
{
	struct vattr va, nva;
	int error;

	/* Make data file name based on FH. */
	error = VOP_GETATTR(vp, &va, cred);
	if (error == 0) {
		/* Set the attributes for "vp" to Setattr the DS vp. */
		VATTR_NULL(&nva);
		nva.va_uid = va.va_uid;
		nva.va_gid = va.va_gid;
		nva.va_mode = va.va_mode;
		nva.va_size = 0;
		VATTR_NULL(&va);
		va.va_type = VREG;
		va.va_mode = nva.va_mode;
		NFSD_DEBUG(4, "nfsrv_dscreatefile: dvp=%p pf=%p\n", dvp, pf);
		error = nfsrv_dscreate(dvp, &va, &nva, fhp, pf, NULL,
		    pf->dsf_filename, cred, p, tvpp);
	}
	return (error);
}

/*
 * Look up the MDS file shared locked, and then get the extended attribute
 * to find the extant DS file to be copied to the new mirror.
 * If successful, *vpp is set to the MDS file's vp and *nvpp is
 * set to a DS data file for the MDS file, both exclusively locked.
 * The "buf" argument has the pnfsdsfile structure from the MDS file
 * in it and buflen is set to its length.
 */
int
nfsrv_mdscopymr(char *mdspathp, char *dspathp, char *curdspathp, char *buf,
    int *buflenp, char *fname, NFSPROC_T *p, struct vnode **vpp,
    struct vnode **nvpp, struct pnfsdsfile **pfp, struct nfsdevice **dsp,
    struct nfsdevice **fdsp)
{
	struct nameidata nd;
	struct vnode *vp, *curvp;
	struct pnfsdsfile *pf;
	struct nfsmount *nmp, *curnmp;
	int dsdir, error, mirrorcnt, ippos;

	vp = NULL;
	curvp = NULL;
	curnmp = NULL;
	*dsp = NULL;
	*fdsp = NULL;
	if (dspathp == NULL && curdspathp != NULL)
		return (EPERM);

	/*
	 * Look up the MDS file shared locked.  The lock will be upgraded
	 * to an exclusive lock after any rw layouts have been returned.
	 */
	NFSD_DEBUG(4, "mdsopen path=%s\n", mdspathp);
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKSHARED | LOCKLEAF, UIO_SYSSPACE,
	    mdspathp, p);
	error = namei(&nd);
	NFSD_DEBUG(4, "lookup=%d\n", error);
	if (error != 0)
		return (error);
	if (nd.ni_vp->v_type != VREG) {
		vput(nd.ni_vp);
		NFSD_DEBUG(4, "mdspath not reg\n");
		return (EISDIR);
	}
	vp = nd.ni_vp;

	if (curdspathp != NULL) {
		/*
		 * Look up the current DS path and find the nfsdev structure for
		 * it.
		 */
		NFSD_DEBUG(4, "curmdsdev path=%s\n", curdspathp);
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKSHARED | LOCKLEAF,
		    UIO_SYSSPACE, curdspathp, p);
		error = namei(&nd);
		NFSD_DEBUG(4, "ds lookup=%d\n", error);
		if (error != 0) {
			vput(vp);
			return (error);
		}
		if (nd.ni_vp->v_type != VDIR) {
			vput(nd.ni_vp);
			vput(vp);
			NFSD_DEBUG(4, "curdspath not dir\n");
			return (ENOTDIR);
		}
		if (strcmp(nd.ni_vp->v_mount->mnt_vfc->vfc_name, "nfs") != 0) {
			vput(nd.ni_vp);
			vput(vp);
			NFSD_DEBUG(4, "curdspath not an NFS mount\n");
			return (ENXIO);
		}
		curnmp = VFSTONFS(nd.ni_vp->v_mount);
	
		/* Search the nfsdev list for a match. */
		NFSDDSLOCK();
		*fdsp = nfsv4_findmirror(curnmp);
		NFSDDSUNLOCK();
		if (*fdsp == NULL)
			curnmp = NULL;
		if (curnmp == NULL) {
			vput(nd.ni_vp);
			vput(vp);
			NFSD_DEBUG(4, "mdscopymr: no current ds\n");
			return (ENXIO);
		}
		curvp = nd.ni_vp;
	}

	if (dspathp != NULL) {
		/* Look up the nfsdev path and find the nfsdev structure. */
		NFSD_DEBUG(4, "mdsdev path=%s\n", dspathp);
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKSHARED | LOCKLEAF,
		    UIO_SYSSPACE, dspathp, p);
		error = namei(&nd);
		NFSD_DEBUG(4, "ds lookup=%d\n", error);
		if (error != 0) {
			vput(vp);
			if (curvp != NULL)
				vput(curvp);
			return (error);
		}
		if (nd.ni_vp->v_type != VDIR || nd.ni_vp == curvp) {
			vput(nd.ni_vp);
			vput(vp);
			if (curvp != NULL)
				vput(curvp);
			NFSD_DEBUG(4, "dspath not dir\n");
			if (nd.ni_vp == curvp)
				return (EPERM);
			return (ENOTDIR);
		}
		if (strcmp(nd.ni_vp->v_mount->mnt_vfc->vfc_name, "nfs") != 0) {
			vput(nd.ni_vp);
			vput(vp);
			if (curvp != NULL)
				vput(curvp);
			NFSD_DEBUG(4, "dspath not an NFS mount\n");
			return (ENXIO);
		}
		nmp = VFSTONFS(nd.ni_vp->v_mount);
	
		/*
		 * Search the nfsdevice list for a match.  If curnmp == NULL,
		 * this is a recovery and there must be a mirror.
		 */
		NFSDDSLOCK();
		if (curnmp == NULL)
			*dsp = nfsrv_findmirroredds(nmp);
		else
			*dsp = nfsv4_findmirror(nmp);
		NFSDDSUNLOCK();
		if (*dsp == NULL) {
			vput(nd.ni_vp);
			vput(vp);
			if (curvp != NULL)
				vput(curvp);
			NFSD_DEBUG(4, "mdscopymr: no ds\n");
			return (ENXIO);
		}
	} else {
		nd.ni_vp = NULL;
		nmp = NULL;
	}

	/*
	 * Get a vp for an available DS data file using the extended
	 * attribute on the MDS file.
	 * If there is a valid entry for the new DS in the extended attribute
	 * on the MDS file (as checked via the nmp argument),
	 * nfsrv_dsgetsockmnt() returns EEXIST, so no copying will occur.
	 */
	error = nfsrv_dsgetsockmnt(vp, 0, buf, buflenp, &mirrorcnt, p,
	    NULL, NULL, NULL, fname, nvpp, &nmp, curnmp, &ippos, &dsdir);
	if (curvp != NULL)
		vput(curvp);
	if (nd.ni_vp == NULL) {
		if (error == 0 && nmp != NULL) {
			/* Search the nfsdev list for a match. */
			NFSDDSLOCK();
			*dsp = nfsrv_findmirroredds(nmp);
			NFSDDSUNLOCK();
		}
		if (error == 0 && (nmp == NULL || *dsp == NULL)) {
			if (nvpp != NULL && *nvpp != NULL) {
				vput(*nvpp);
				*nvpp = NULL;
			}
			error = ENXIO;
		}
	} else
		vput(nd.ni_vp);

	/*
	 * When dspathp != NULL and curdspathp == NULL, this is a recovery
	 * and is only allowed if there is a 0.0.0.0 IP address entry.
	 * When curdspathp != NULL, the ippos will be set to that entry.
	 */
	if (error == 0 && dspathp != NULL && ippos == -1) {
		if (nvpp != NULL && *nvpp != NULL) {
			vput(*nvpp);
			*nvpp = NULL;
		}
		error = ENXIO;
	}
	if (error == 0) {
		*vpp = vp;

		pf = (struct pnfsdsfile *)buf;
		if (ippos == -1) {
			/* If no zeroip pnfsdsfile, add one. */
			ippos = *buflenp / sizeof(*pf);
			*buflenp += sizeof(*pf);
			pf += ippos;
			pf->dsf_dir = dsdir;
			strlcpy(pf->dsf_filename, fname,
			    sizeof(pf->dsf_filename));
		} else
			pf += ippos;
		*pfp = pf;
	} else
		vput(vp);
	return (error);
}

/*
 * Search for a matching pnfsd mirror device structure, base on the nmp arg.
 * Return one if found, NULL otherwise.
 */
static struct nfsdevice *
nfsrv_findmirroredds(struct nfsmount *nmp)
{
	struct nfsdevice *ds, *fndds;
	int fndmirror;

	mtx_assert(NFSDDSMUTEXPTR, MA_OWNED);
	/*
	 * Search the DS server list for a match with nmp.
	 * Remove the DS entry if found and there is a mirror.
	 */
	fndds = NULL;
	fndmirror = 0;
	if (nfsrv_devidcnt == 0)
		return (fndds);
	TAILQ_FOREACH(ds, &nfsrv_devidhead, nfsdev_list) {
		if (ds->nfsdev_nmp == nmp) {
			NFSD_DEBUG(4, "nfsrv_findmirroredds: fnd main ds\n");
			fndds = ds;
			break;
		}
	}
	if (fndds == NULL)
		return (fndds);
	if (fndds->nfsdev_mdsisset == 0 && nfsrv_faildscnt > 0)
		fndmirror = 1;
	else if (fndds->nfsdev_mdsisset != 0) {
		/* For the fsid is set case, search for a mirror. */
		TAILQ_FOREACH(ds, &nfsrv_devidhead, nfsdev_list) {
			if (ds != fndds && ds->nfsdev_nmp != NULL &&
			    ds->nfsdev_mdsisset != 0 &&
			    ds->nfsdev_mdsfsid.val[0] ==
			    fndds->nfsdev_mdsfsid.val[0] &&
			    ds->nfsdev_mdsfsid.val[1] ==
			    fndds->nfsdev_mdsfsid.val[1]) {
				fndmirror = 1;
				break;
			}
		}
	}
	if (fndmirror == 0) {
		NFSD_DEBUG(4, "nfsrv_findmirroredds: no mirror for DS\n");
		return (NULL);
	}
	return (fndds);
}

