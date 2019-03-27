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

/*
 * These functions support the macros and help fiddle mbuf chains for
 * the nfs op functions. They do things like create the rpc header and
 * copy data between mbuf chains and uio lists.
 */
#ifndef APPLEKEXT
#include "opt_inet6.h"

#include <fs/nfs/nfsport.h>

#include <security/mac/mac_framework.h>

/*
 * Data items converted to xdr at startup, since they are constant
 * This is kinda hokey, but may save a little time doing byte swaps
 */
u_int32_t newnfs_true, newnfs_false, newnfs_xdrneg1;

/* And other global data */
nfstype nfsv34_type[9] = { NFNON, NFREG, NFDIR, NFBLK, NFCHR, NFLNK, NFSOCK,
		      NFFIFO, NFNON };
enum vtype newnv2tov_type[8] = { VNON, VREG, VDIR, VBLK, VCHR, VLNK, VNON, VNON };
enum vtype nv34tov_type[8]={ VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK, VFIFO };
struct timeval nfsboottime;	/* Copy boottime once, so it never changes */
int nfscl_ticks;
int nfsrv_useacl = 1;
struct nfssockreq nfsrv_nfsuserdsock;
int nfsrv_nfsuserd = 0;
struct nfsreqhead nfsd_reqq;
uid_t nfsrv_defaultuid = UID_NOBODY;
gid_t nfsrv_defaultgid = GID_NOGROUP;
int nfsrv_lease = NFSRV_LEASE;
int ncl_mbuf_mlen = MLEN;
int nfsd_enable_stringtouid = 0;
int nfsrv_doflexfile = 0;
static int nfs_enable_uidtostring = 0;
NFSNAMEIDMUTEX;
NFSSOCKMUTEX;
extern int nfsrv_lughashsize;
extern struct mtx nfsrv_dslock_mtx;
extern volatile int nfsrv_devidcnt;
extern int nfscl_debuglevel;
extern struct nfsdevicehead nfsrv_devidhead;
extern struct nfsstatsv1 nfsstatsv1;

SYSCTL_DECL(_vfs_nfs);
SYSCTL_INT(_vfs_nfs, OID_AUTO, enable_uidtostring, CTLFLAG_RW,
    &nfs_enable_uidtostring, 0, "Make nfs always send numeric owner_names");

int nfsrv_maxpnfsmirror = 1;
SYSCTL_INT(_vfs_nfs, OID_AUTO, pnfsmirror, CTLFLAG_RD,
    &nfsrv_maxpnfsmirror, 0, "Mirror level for pNFS service");

/*
 * This array of structures indicates, for V4:
 * retfh - which of 3 types of calling args are used
 *	0 - doesn't change cfh or use a sfh
 *	1 - replaces cfh with a new one (unless it returns an error status)
 *	2 - uses cfh and sfh
 * needscfh - if the op wants a cfh and premtime
 *	0 - doesn't use a cfh
 *	1 - uses a cfh, but doesn't want pre-op attributes
 *	2 - uses a cfh and wants pre-op attributes
 * savereply - indicates a non-idempotent Op
 *	0 - not non-idempotent
 *	1 - non-idempotent
 * Ops that are ordered via seqid# are handled separately from these
 * non-idempotent Ops.
 * Define it here, since it is used by both the client and server.
 */
struct nfsv4_opflag nfsv4_opflag[NFSV41_NOPS] = {
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* undef */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* undef */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* undef */
	{ 0, 1, 0, 0, LK_SHARED, 1, 1 },		/* Access */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* Close */
	{ 0, 2, 0, 1, LK_EXCLUSIVE, 1, 1 },		/* Commit */
	{ 1, 2, 1, 1, LK_EXCLUSIVE, 1, 1 },		/* Create */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* Delegpurge */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* Delegreturn */
	{ 0, 1, 0, 0, LK_SHARED, 1, 1 },		/* Getattr */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* GetFH */
	{ 2, 1, 1, 1, LK_EXCLUSIVE, 1, 1 },		/* Link */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* Lock */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* LockT */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* LockU */
	{ 1, 2, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Lookup */
	{ 1, 2, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Lookupp */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* NVerify */
	{ 1, 1, 0, 1, LK_EXCLUSIVE, 1, 0 },		/* Open */
	{ 1, 1, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* OpenAttr */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* OpenConfirm */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* OpenDowngrade */
	{ 1, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* PutFH */
	{ 1, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* PutPubFH */
	{ 1, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* PutRootFH */
	{ 0, 1, 0, 0, LK_SHARED, 1, 0 },		/* Read */
	{ 0, 1, 0, 0, LK_SHARED, 1, 1 },		/* Readdir */
	{ 0, 1, 0, 0, LK_SHARED, 1, 1 },		/* ReadLink */
	{ 0, 2, 1, 1, LK_EXCLUSIVE, 1, 1 },		/* Remove */
	{ 2, 1, 1, 1, LK_EXCLUSIVE, 1, 1 },		/* Rename */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* Renew */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* RestoreFH */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* SaveFH */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* SecInfo */
	{ 0, 2, 1, 1, LK_EXCLUSIVE, 1, 0 },		/* Setattr */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* SetClientID */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* SetClientIDConfirm */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Verify */
	{ 0, 2, 1, 1, LK_EXCLUSIVE, 1, 0 },		/* Write */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* ReleaseLockOwner */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Backchannel Ctrl */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 0, 0 },		/* Bind Conn to Sess */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 0, 0 },		/* Exchange ID */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 0, 0 },		/* Create Session */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 0, 0 },		/* Destroy Session */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* Free StateID */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Get Dir Deleg */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Get Device Info */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Get Device List */
	{ 0, 1, 0, 1, LK_EXCLUSIVE, 1, 1 },		/* Layout Commit */
	{ 0, 1, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Layout Get */
	{ 0, 1, 0, 1, LK_EXCLUSIVE, 1, 0 },		/* Layout Return */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Secinfo No name */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* Sequence */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Set SSV */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Test StateID */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 1 },		/* Want Delegation */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 0, 0 },		/* Destroy ClientID */
	{ 0, 0, 0, 0, LK_EXCLUSIVE, 1, 0 },		/* Reclaim Complete */
};
#endif	/* !APPLEKEXT */

static int ncl_mbuf_mhlen = MHLEN;
static int nfsrv_usercnt = 0;
static int nfsrv_dnsnamelen;
static u_char *nfsrv_dnsname = NULL;
static int nfsrv_usermax = 999999999;
struct nfsrv_lughash {
	struct mtx		mtx;
	struct nfsuserhashhead	lughead;
};
static struct nfsrv_lughash	*nfsuserhash;
static struct nfsrv_lughash	*nfsusernamehash;
static struct nfsrv_lughash	*nfsgrouphash;
static struct nfsrv_lughash	*nfsgroupnamehash;

/*
 * This static array indicates whether or not the RPC generates a large
 * reply. This is used by nfs_reply() to decide whether or not an mbuf
 * cluster should be allocated. (If a cluster is required by an RPC
 * marked 0 in this array, the code will still work, just not quite as
 * efficiently.)
 */
static int nfs_bigreply[NFSV41_NPROCS] = { 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };

/* local functions */
static int nfsrv_skipace(struct nfsrv_descript *nd, int *acesizep);
static void nfsv4_wanted(struct nfsv4lock *lp);
static int nfsrv_cmpmixedcase(u_char *cp, u_char *cp2, int len);
static int nfsrv_getuser(int procnum, uid_t uid, gid_t gid, char *name);
static void nfsrv_removeuser(struct nfsusrgrp *usrp, int isuser);
static int nfsrv_getrefstr(struct nfsrv_descript *, u_char **, u_char **,
    int *, int *);
static void nfsrv_refstrbigenough(int, u_char **, u_char **, int *);

static struct {
	int	op;
	int	opcnt;
	const u_char *tag;
	int	taglen;
} nfsv4_opmap[NFSV41_NPROCS] = {
	{ 0, 1, "Null", 4 },
	{ NFSV4OP_GETATTR, 1, "Getattr", 7, },
	{ NFSV4OP_SETATTR, 2, "Setattr", 7, },
	{ NFSV4OP_LOOKUP, 3, "Lookup", 6, },
	{ NFSV4OP_ACCESS, 2, "Access", 6, },
	{ NFSV4OP_READLINK, 2, "Readlink", 8, },
	{ NFSV4OP_READ, 1, "Read", 4, },
	{ NFSV4OP_WRITE, 2, "Write", 5, },
	{ NFSV4OP_OPEN, 5, "Open", 4, },
	{ NFSV4OP_CREATE, 5, "Create", 6, },
	{ NFSV4OP_CREATE, 1, "Create", 6, },
	{ NFSV4OP_CREATE, 3, "Create", 6, },
	{ NFSV4OP_REMOVE, 1, "Remove", 6, },
	{ NFSV4OP_REMOVE, 1, "Remove", 6, },
	{ NFSV4OP_SAVEFH, 5, "Rename", 6, },
	{ NFSV4OP_SAVEFH, 4, "Link", 4, },
	{ NFSV4OP_READDIR, 2, "Readdir", 7, },
	{ NFSV4OP_READDIR, 2, "Readdir", 7, },
	{ NFSV4OP_GETATTR, 1, "Getattr", 7, },
	{ NFSV4OP_GETATTR, 1, "Getattr", 7, },
	{ NFSV4OP_GETATTR, 1, "Getattr", 7, },
	{ NFSV4OP_COMMIT, 2, "Commit", 6, },
	{ NFSV4OP_LOOKUPP, 3, "Lookupp", 7, },
	{ NFSV4OP_SETCLIENTID, 1, "SetClientID", 11, },
	{ NFSV4OP_SETCLIENTIDCFRM, 1, "SetClientIDConfirm", 18, },
	{ NFSV4OP_LOCK, 1, "Lock", 4, },
	{ NFSV4OP_LOCKU, 1, "LockU", 5, },
	{ NFSV4OP_OPEN, 2, "Open", 4, },
	{ NFSV4OP_CLOSE, 1, "Close", 5, },
	{ NFSV4OP_OPENCONFIRM, 1, "Openconfirm", 11, },
	{ NFSV4OP_LOCKT, 1, "LockT", 5, },
	{ NFSV4OP_OPENDOWNGRADE, 1, "Opendowngrade", 13, },
	{ NFSV4OP_RENEW, 1, "Renew", 5, },
	{ NFSV4OP_PUTROOTFH, 1, "Dirpath", 7, },
	{ NFSV4OP_RELEASELCKOWN, 1, "Rellckown", 9, },
	{ NFSV4OP_DELEGRETURN, 1, "Delegret", 8, },
	{ NFSV4OP_DELEGRETURN, 3, "DelegRemove", 11, },
	{ NFSV4OP_DELEGRETURN, 7, "DelegRename1", 12, },
	{ NFSV4OP_DELEGRETURN, 9, "DelegRename2", 12, },
	{ NFSV4OP_GETATTR, 1, "Getacl", 6, },
	{ NFSV4OP_SETATTR, 1, "Setacl", 6, },
	{ NFSV4OP_EXCHANGEID, 1, "ExchangeID", 10, },
	{ NFSV4OP_CREATESESSION, 1, "CreateSession", 13, },
	{ NFSV4OP_DESTROYSESSION, 1, "DestroySession", 14, },
	{ NFSV4OP_DESTROYCLIENTID, 1, "DestroyClient", 13, },
	{ NFSV4OP_FREESTATEID, 1, "FreeStateID", 11, },
	{ NFSV4OP_LAYOUTGET, 1, "LayoutGet", 9, },
	{ NFSV4OP_GETDEVINFO, 1, "GetDeviceInfo", 13, },
	{ NFSV4OP_LAYOUTCOMMIT, 1, "LayoutCommit", 12, },
	{ NFSV4OP_LAYOUTRETURN, 1, "LayoutReturn", 12, },
	{ NFSV4OP_RECLAIMCOMPL, 1, "ReclaimComplete", 15, },
	{ NFSV4OP_WRITE, 1, "WriteDS", 7, },
	{ NFSV4OP_READ, 1, "ReadDS", 6, },
	{ NFSV4OP_COMMIT, 1, "CommitDS", 8, },
	{ NFSV4OP_OPEN, 3, "OpenLayoutGet", 13, },
	{ NFSV4OP_OPEN, 8, "CreateLayGet", 12, },
};

/*
 * NFS RPCS that have large request message size.
 */
static int nfs_bigrequest[NFSV41_NPROCS] = {
	0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0
};

/*
 * Start building a request. Mostly just put the first file handle in
 * place.
 */
APPLESTATIC void
nfscl_reqstart(struct nfsrv_descript *nd, int procnum, struct nfsmount *nmp,
    u_int8_t *nfhp, int fhlen, u_int32_t **opcntpp, struct nfsclsession *sep,
    int vers, int minorvers)
{
	struct mbuf *mb;
	u_int32_t *tl;
	int opcnt;
	nfsattrbit_t attrbits;

	/*
	 * First, fill in some of the fields of nd.
	 */
	nd->nd_slotseq = NULL;
	if (vers == NFS_VER4) {
		nd->nd_flag = ND_NFSV4 | ND_NFSCL;
		if (minorvers == NFSV41_MINORVERSION)
			nd->nd_flag |= ND_NFSV41;
	} else if (vers == NFS_VER3)
		nd->nd_flag = ND_NFSV3 | ND_NFSCL;
	else {
		if (NFSHASNFSV4(nmp)) {
			nd->nd_flag = ND_NFSV4 | ND_NFSCL;
			if (NFSHASNFSV4N(nmp))
				nd->nd_flag |= ND_NFSV41;
		} else if (NFSHASNFSV3(nmp))
			nd->nd_flag = ND_NFSV3 | ND_NFSCL;
		else
			nd->nd_flag = ND_NFSV2 | ND_NFSCL;
	}
	nd->nd_procnum = procnum;
	nd->nd_repstat = 0;

	/*
	 * Get the first mbuf for the request.
	 */
	if (nfs_bigrequest[procnum])
		NFSMCLGET(mb, M_WAITOK);
	else
		NFSMGET(mb);
	mbuf_setlen(mb, 0);
	nd->nd_mreq = nd->nd_mb = mb;
	nd->nd_bpos = NFSMTOD(mb, caddr_t);
	
	/*
	 * And fill the first file handle into the request.
	 */
	if (nd->nd_flag & ND_NFSV4) {
		opcnt = nfsv4_opmap[procnum].opcnt +
		    nfsv4_opflag[nfsv4_opmap[procnum].op].needscfh;
		if ((nd->nd_flag & ND_NFSV41) != 0) {
			opcnt += nfsv4_opflag[nfsv4_opmap[procnum].op].needsseq;
			if (procnum == NFSPROC_RENEW)
				/*
				 * For the special case of Renew, just do a
				 * Sequence Op.
				 */
				opcnt = 1;
			else if (procnum == NFSPROC_WRITEDS ||
			    procnum == NFSPROC_COMMITDS)
				/*
				 * For the special case of a Writeor Commit to
				 * a DS, the opcnt == 3, for Sequence, PutFH,
				 * Write/Commit.
				 */
				opcnt = 3;
		}
		/*
		 * What should the tag really be?
		 */
		(void) nfsm_strtom(nd, nfsv4_opmap[procnum].tag,
			nfsv4_opmap[procnum].taglen);
		NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
		if ((nd->nd_flag & ND_NFSV41) != 0)
			*tl++ = txdr_unsigned(NFSV41_MINORVERSION);
		else
			*tl++ = txdr_unsigned(NFSV4_MINORVERSION);
		if (opcntpp != NULL)
			*opcntpp = tl;
		*tl = txdr_unsigned(opcnt);
		if ((nd->nd_flag & ND_NFSV41) != 0 &&
		    nfsv4_opflag[nfsv4_opmap[procnum].op].needsseq > 0) {
			if (nfsv4_opflag[nfsv4_opmap[procnum].op].loopbadsess >
			    0)
				nd->nd_flag |= ND_LOOPBADSESS;
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV4OP_SEQUENCE);
			if (sep == NULL) {
				sep = nfsmnt_mdssession(nmp);
				nfsv4_setsequence(nmp, nd, sep,
				    nfs_bigreply[procnum]);
			} else
				nfsv4_setsequence(nmp, nd, sep,
				    nfs_bigreply[procnum]);
		}
		if (nfsv4_opflag[nfsv4_opmap[procnum].op].needscfh > 0) {
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV4OP_PUTFH);
			(void) nfsm_fhtom(nd, nfhp, fhlen, 0);
			if (nfsv4_opflag[nfsv4_opmap[procnum].op].needscfh
			    == 2 && procnum != NFSPROC_WRITEDS &&
			    procnum != NFSPROC_COMMITDS) {
				NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
				*tl = txdr_unsigned(NFSV4OP_GETATTR);
				/*
				 * For Lookup Ops, we want all the directory
				 * attributes, so we can load the name cache.
				 */
				if (procnum == NFSPROC_LOOKUP ||
				    procnum == NFSPROC_LOOKUPP)
					NFSGETATTR_ATTRBIT(&attrbits);
				else {
					NFSWCCATTR_ATTRBIT(&attrbits);
					nd->nd_flag |= ND_V4WCCATTR;
				}
				(void) nfsrv_putattrbit(nd, &attrbits);
			}
		}
		if (procnum != NFSPROC_RENEW ||
		    (nd->nd_flag & ND_NFSV41) == 0) {
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(nfsv4_opmap[procnum].op);
		}
	} else {
		(void) nfsm_fhtom(nd, nfhp, fhlen, 0);
	}
	if (procnum < NFSV41_NPROCS)
		NFSINCRGLOBAL(nfsstatsv1.rpccnt[procnum]);
}

/*
 * Put a state Id in the mbuf list.
 */
APPLESTATIC void
nfsm_stateidtom(struct nfsrv_descript *nd, nfsv4stateid_t *stateidp, int flag)
{
	nfsv4stateid_t *st;

	NFSM_BUILD(st, nfsv4stateid_t *, NFSX_STATEID);
	if (flag == NFSSTATEID_PUTALLZERO) {
		st->seqid = 0;
		st->other[0] = 0;
		st->other[1] = 0;
		st->other[2] = 0;
	} else if (flag == NFSSTATEID_PUTALLONE) {
		st->seqid = 0xffffffff;
		st->other[0] = 0xffffffff;
		st->other[1] = 0xffffffff;
		st->other[2] = 0xffffffff;
	} else if (flag == NFSSTATEID_PUTSEQIDZERO) {
		st->seqid = 0;
		st->other[0] = stateidp->other[0];
		st->other[1] = stateidp->other[1];
		st->other[2] = stateidp->other[2];
	} else {
		st->seqid = stateidp->seqid;
		st->other[0] = stateidp->other[0];
		st->other[1] = stateidp->other[1];
		st->other[2] = stateidp->other[2];
	}
}

/*
 * Fill in the setable attributes. The full argument indicates whether
 * to fill in them all or just mode and time.
 */
void
nfscl_fillsattr(struct nfsrv_descript *nd, struct vattr *vap,
    struct vnode *vp, int flags, u_int32_t rdev)
{
	u_int32_t *tl;
	struct nfsv2_sattr *sp;
	nfsattrbit_t attrbits;

	switch (nd->nd_flag & (ND_NFSV2 | ND_NFSV3 | ND_NFSV4)) {
	case ND_NFSV2:
		NFSM_BUILD(sp, struct nfsv2_sattr *, NFSX_V2SATTR);
		if (vap->va_mode == (mode_t)VNOVAL)
			sp->sa_mode = newnfs_xdrneg1;
		else
			sp->sa_mode = vtonfsv2_mode(vap->va_type, vap->va_mode);
		if (vap->va_uid == (uid_t)VNOVAL)
			sp->sa_uid = newnfs_xdrneg1;
		else
			sp->sa_uid = txdr_unsigned(vap->va_uid);
		if (vap->va_gid == (gid_t)VNOVAL)
			sp->sa_gid = newnfs_xdrneg1;
		else
			sp->sa_gid = txdr_unsigned(vap->va_gid);
		if (flags & NFSSATTR_SIZE0)
			sp->sa_size = 0;
		else if (flags & NFSSATTR_SIZENEG1)
			sp->sa_size = newnfs_xdrneg1;
		else if (flags & NFSSATTR_SIZERDEV)
			sp->sa_size = txdr_unsigned(rdev);
		else
			sp->sa_size = txdr_unsigned(vap->va_size);
		txdr_nfsv2time(&vap->va_atime, &sp->sa_atime);
		txdr_nfsv2time(&vap->va_mtime, &sp->sa_mtime);
		break;
	case ND_NFSV3:
		if (vap->va_mode != (mode_t)VNOVAL) {
			NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			*tl++ = newnfs_true;
			*tl = txdr_unsigned(vap->va_mode);
		} else {
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_false;
		}
		if ((flags & NFSSATTR_FULL) && vap->va_uid != (uid_t)VNOVAL) {
			NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			*tl++ = newnfs_true;
			*tl = txdr_unsigned(vap->va_uid);
		} else {
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_false;
		}
		if ((flags & NFSSATTR_FULL) && vap->va_gid != (gid_t)VNOVAL) {
			NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			*tl++ = newnfs_true;
			*tl = txdr_unsigned(vap->va_gid);
		} else {
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_false;
		}
		if ((flags & NFSSATTR_FULL) && vap->va_size != VNOVAL) {
			NFSM_BUILD(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
			*tl++ = newnfs_true;
			txdr_hyper(vap->va_size, tl);
		} else {
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_false;
		}
		if (vap->va_atime.tv_sec != VNOVAL) {
			if ((vap->va_vaflags & VA_UTIMES_NULL) == 0) {
				NFSM_BUILD(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
				*tl++ = txdr_unsigned(NFSV3SATTRTIME_TOCLIENT);
				txdr_nfsv3time(&vap->va_atime, tl);
			} else {
				NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
				*tl = txdr_unsigned(NFSV3SATTRTIME_TOSERVER);
			}
		} else {
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV3SATTRTIME_DONTCHANGE);
		}
		if (vap->va_mtime.tv_sec != VNOVAL) {
			if ((vap->va_vaflags & VA_UTIMES_NULL) == 0) {
				NFSM_BUILD(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
				*tl++ = txdr_unsigned(NFSV3SATTRTIME_TOCLIENT);
				txdr_nfsv3time(&vap->va_mtime, tl);
			} else {
				NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
				*tl = txdr_unsigned(NFSV3SATTRTIME_TOSERVER);
			}
		} else {
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV3SATTRTIME_DONTCHANGE);
		}
		break;
	case ND_NFSV4:
		NFSZERO_ATTRBIT(&attrbits);
		if (vap->va_mode != (mode_t)VNOVAL)
			NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_MODE);
		if ((flags & NFSSATTR_FULL) && vap->va_uid != (uid_t)VNOVAL)
			NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_OWNER);
		if ((flags & NFSSATTR_FULL) && vap->va_gid != (gid_t)VNOVAL)
			NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_OWNERGROUP);
		if ((flags & NFSSATTR_FULL) && vap->va_size != VNOVAL)
			NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_SIZE);
		if (vap->va_atime.tv_sec != VNOVAL)
			NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_TIMEACCESSSET);
		if (vap->va_mtime.tv_sec != VNOVAL)
			NFSSETBIT_ATTRBIT(&attrbits, NFSATTRBIT_TIMEMODIFYSET);
		(void) nfsv4_fillattr(nd, vp->v_mount, vp, NULL, vap, NULL, 0,
		    &attrbits, NULL, NULL, 0, 0, 0, 0, (uint64_t)0, NULL);
		break;
	}
}

#ifndef APPLE
/*
 * copies mbuf chain to the uio scatter/gather list
 */
int
nfsm_mbufuio(struct nfsrv_descript *nd, struct uio *uiop, int siz)
{
	char *mbufcp, *uiocp;
	int xfer, left, len;
	mbuf_t mp;
	long uiosiz, rem;
	int error = 0;

	mp = nd->nd_md;
	mbufcp = nd->nd_dpos;
	len = NFSMTOD(mp, caddr_t) + mbuf_len(mp) - mbufcp;
	rem = NFSM_RNDUP(siz) - siz;
	while (siz > 0) {
		if (uiop->uio_iovcnt <= 0 || uiop->uio_iov == NULL) {
			error = EBADRPC;
			goto out;
		}
		left = uiop->uio_iov->iov_len;
		uiocp = uiop->uio_iov->iov_base;
		if (left > siz)
			left = siz;
		uiosiz = left;
		while (left > 0) {
			while (len == 0) {
				mp = mbuf_next(mp);
				if (mp == NULL) {
					error = EBADRPC;
					goto out;
				}
				mbufcp = NFSMTOD(mp, caddr_t);
				len = mbuf_len(mp);
				KASSERT(len >= 0,
				    ("len %d, corrupted mbuf?", len));
			}
			xfer = (left > len) ? len : left;
#ifdef notdef
			/* Not Yet.. */
			if (uiop->uio_iov->iov_op != NULL)
				(*(uiop->uio_iov->iov_op))
				(mbufcp, uiocp, xfer);
			else
#endif
			if (uiop->uio_segflg == UIO_SYSSPACE)
				NFSBCOPY(mbufcp, uiocp, xfer);
			else
				copyout(mbufcp, CAST_USER_ADDR_T(uiocp), xfer);
			left -= xfer;
			len -= xfer;
			mbufcp += xfer;
			uiocp += xfer;
			uiop->uio_offset += xfer;
			uiop->uio_resid -= xfer;
		}
		if (uiop->uio_iov->iov_len <= siz) {
			uiop->uio_iovcnt--;
			uiop->uio_iov++;
		} else {
			uiop->uio_iov->iov_base = (void *)
				((char *)uiop->uio_iov->iov_base + uiosiz);
			uiop->uio_iov->iov_len -= uiosiz;
		}
		siz -= uiosiz;
	}
	nd->nd_dpos = mbufcp;
	nd->nd_md = mp;
	if (rem > 0) {
		if (len < rem)
			error = nfsm_advance(nd, rem, len);
		else
			nd->nd_dpos += rem;
	}

out:
	NFSEXITCODE2(error, nd);
	return (error);
}
#endif	/* !APPLE */

/*
 * Help break down an mbuf chain by setting the first siz bytes contiguous
 * pointed to by returned val.
 * This is used by the macro NFSM_DISSECT for tough
 * cases.
 */
APPLESTATIC void *
nfsm_dissct(struct nfsrv_descript *nd, int siz, int how)
{
	mbuf_t mp2;
	int siz2, xfer;
	caddr_t p;
	int left;
	caddr_t retp;

	retp = NULL;
	left = NFSMTOD(nd->nd_md, caddr_t) + mbuf_len(nd->nd_md) - nd->nd_dpos;
	while (left == 0) {
		nd->nd_md = mbuf_next(nd->nd_md);
		if (nd->nd_md == NULL)
			return (retp);
		left = mbuf_len(nd->nd_md);
		nd->nd_dpos = NFSMTOD(nd->nd_md, caddr_t);
	}
	if (left >= siz) {
		retp = nd->nd_dpos;
		nd->nd_dpos += siz;
	} else if (mbuf_next(nd->nd_md) == NULL) {
		return (retp);
	} else if (siz > ncl_mbuf_mhlen) {
		panic("nfs S too big");
	} else {
		MGET(mp2, MT_DATA, how);
		if (mp2 == NULL)
			return (NULL);
		mbuf_setnext(mp2, mbuf_next(nd->nd_md));
		mbuf_setnext(nd->nd_md, mp2);
		mbuf_setlen(nd->nd_md, mbuf_len(nd->nd_md) - left);
		nd->nd_md = mp2;
		retp = p = NFSMTOD(mp2, caddr_t);
		NFSBCOPY(nd->nd_dpos, p, left);	/* Copy what was left */
		siz2 = siz - left;
		p += left;
		mp2 = mbuf_next(mp2);
		/* Loop around copying up the siz2 bytes */
		while (siz2 > 0) {
			if (mp2 == NULL)
				return (NULL);
			xfer = (siz2 > mbuf_len(mp2)) ? mbuf_len(mp2) : siz2;
			if (xfer > 0) {
				NFSBCOPY(NFSMTOD(mp2, caddr_t), p, xfer);
				NFSM_DATAP(mp2, xfer);
				mbuf_setlen(mp2, mbuf_len(mp2) - xfer);
				p += xfer;
				siz2 -= xfer;
			}
			if (siz2 > 0)
				mp2 = mbuf_next(mp2);
		}
		mbuf_setlen(nd->nd_md, siz);
		nd->nd_md = mp2;
		nd->nd_dpos = NFSMTOD(mp2, caddr_t);
	}
	return (retp);
}

/*
 * Advance the position in the mbuf chain.
 * If offs == 0, this is a no-op, but it is simpler to just return from
 * here than check for offs > 0 for all calls to nfsm_advance.
 * If left == -1, it should be calculated here.
 */
APPLESTATIC int
nfsm_advance(struct nfsrv_descript *nd, int offs, int left)
{
	int error = 0;

	if (offs == 0)
		goto out;
	/*
	 * A negative offs might indicate a corrupted mbuf chain and,
	 * as such, a printf is logged.
	 */
	if (offs < 0) {
		printf("nfsrv_advance: negative offs\n");
		error = EBADRPC;
		goto out;
	}

	/*
	 * If left == -1, calculate it here.
	 */
	if (left == -1)
		left = NFSMTOD(nd->nd_md, caddr_t) + mbuf_len(nd->nd_md) -
		    nd->nd_dpos;

	/*
	 * Loop around, advancing over the mbuf data.
	 */
	while (offs > left) {
		offs -= left;
		nd->nd_md = mbuf_next(nd->nd_md);
		if (nd->nd_md == NULL) {
			error = EBADRPC;
			goto out;
		}
		left = mbuf_len(nd->nd_md);
		nd->nd_dpos = NFSMTOD(nd->nd_md, caddr_t);
	}
	nd->nd_dpos += offs;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Copy a string into mbuf(s).
 * Return the number of bytes output, including XDR overheads.
 */
APPLESTATIC int
nfsm_strtom(struct nfsrv_descript *nd, const char *cp, int siz)
{
	mbuf_t m2;
	int xfer, left;
	mbuf_t m1;
	int rem, bytesize;
	u_int32_t *tl;
	char *cp2;

	NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
	*tl = txdr_unsigned(siz);
	rem = NFSM_RNDUP(siz) - siz;
	bytesize = NFSX_UNSIGNED + siz + rem;
	m2 = nd->nd_mb;
	cp2 = nd->nd_bpos;
	left = M_TRAILINGSPACE(m2);

	/*
	 * Loop around copying the string to mbuf(s).
	 */
	while (siz > 0) {
		if (left == 0) {
			if (siz > ncl_mbuf_mlen)
				NFSMCLGET(m1, M_WAITOK);
			else
				NFSMGET(m1);
			mbuf_setlen(m1, 0);
			mbuf_setnext(m2, m1);
			m2 = m1;
			cp2 = NFSMTOD(m2, caddr_t);
			left = M_TRAILINGSPACE(m2);
		}
		if (left >= siz)
			xfer = siz;
		else
			xfer = left;
		NFSBCOPY(cp, cp2, xfer);
		cp += xfer;
		mbuf_setlen(m2, mbuf_len(m2) + xfer);
		siz -= xfer;
		left -= xfer;
		if (siz == 0 && rem) {
			if (left < rem)
				panic("nfsm_strtom");
			NFSBZERO(cp2 + xfer, rem);
			mbuf_setlen(m2, mbuf_len(m2) + rem);
		}
	}
	nd->nd_mb = m2;
	nd->nd_bpos = NFSMTOD(m2, caddr_t) + mbuf_len(m2);
	return (bytesize);
}

/*
 * Called once to initialize data structures...
 */
APPLESTATIC void
newnfs_init(void)
{
	static int nfs_inited = 0;

	if (nfs_inited)
		return;
	nfs_inited = 1;

	newnfs_true = txdr_unsigned(TRUE);
	newnfs_false = txdr_unsigned(FALSE);
	newnfs_xdrneg1 = txdr_unsigned(-1);
	nfscl_ticks = (hz * NFS_TICKINTVL + 500) / 1000;
	if (nfscl_ticks < 1)
		nfscl_ticks = 1;
	NFSSETBOOTTIME(nfsboottime);

	/*
	 * Initialize reply list and start timer
	 */
	TAILQ_INIT(&nfsd_reqq);
	NFS_TIMERINIT;
}

/*
 * Put a file handle in an mbuf list.
 * If the size argument == 0, just use the default size.
 * set_true == 1 if there should be an newnfs_true prepended on the file handle.
 * Return the number of bytes output, including XDR overhead.
 */
APPLESTATIC int
nfsm_fhtom(struct nfsrv_descript *nd, u_int8_t *fhp, int size, int set_true)
{
	u_int32_t *tl;
	u_int8_t *cp;
	int fullsiz, rem, bytesize = 0;

	if (size == 0)
		size = NFSX_MYFH;
	switch (nd->nd_flag & (ND_NFSV2 | ND_NFSV3 | ND_NFSV4)) {
	case ND_NFSV2:
		if (size > NFSX_V2FH)
			panic("fh size > NFSX_V2FH for NFSv2");
		NFSM_BUILD(cp, u_int8_t *, NFSX_V2FH);
		NFSBCOPY(fhp, cp, size);
		if (size < NFSX_V2FH)
			NFSBZERO(cp + size, NFSX_V2FH - size);
		bytesize = NFSX_V2FH;
		break;
	case ND_NFSV3:
	case ND_NFSV4:
		fullsiz = NFSM_RNDUP(size);
		rem = fullsiz - size;
		if (set_true) {
		    bytesize = 2 * NFSX_UNSIGNED + fullsiz;
		    NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
		    *tl = newnfs_true;
		} else {
		    bytesize = NFSX_UNSIGNED + fullsiz;
		}
		(void) nfsm_strtom(nd, fhp, size);
		break;
	}
	return (bytesize);
}

/*
 * This function compares two net addresses by family and returns TRUE
 * if they are the same host.
 * If there is any doubt, return FALSE.
 * The AF_INET family is handled as a special case so that address mbufs
 * don't need to be saved to store "struct in_addr", which is only 4 bytes.
 */
APPLESTATIC int
nfsaddr_match(int family, union nethostaddr *haddr, NFSSOCKADDR_T nam)
{
	struct sockaddr_in *inetaddr;

	switch (family) {
	case AF_INET:
		inetaddr = NFSSOCKADDR(nam, struct sockaddr_in *);
		if (inetaddr->sin_family == AF_INET &&
		    inetaddr->sin_addr.s_addr == haddr->had_inet.s_addr)
			return (1);
		break;
#ifdef INET6
	case AF_INET6:
		{
		struct sockaddr_in6 *inetaddr6;

		inetaddr6 = NFSSOCKADDR(nam, struct sockaddr_in6 *);
		/* XXX - should test sin6_scope_id ? */
		if (inetaddr6->sin6_family == AF_INET6 &&
		    IN6_ARE_ADDR_EQUAL(&inetaddr6->sin6_addr,
			  &haddr->had_inet6))
			return (1);
		}
		break;
#endif
	}
	return (0);
}

/*
 * Similar to the above, but takes to NFSSOCKADDR_T args.
 */
APPLESTATIC int
nfsaddr2_match(NFSSOCKADDR_T nam1, NFSSOCKADDR_T nam2)
{
	struct sockaddr_in *addr1, *addr2;
	struct sockaddr *inaddr;

	inaddr = NFSSOCKADDR(nam1, struct sockaddr *);
	switch (inaddr->sa_family) {
	case AF_INET:
		addr1 = NFSSOCKADDR(nam1, struct sockaddr_in *);
		addr2 = NFSSOCKADDR(nam2, struct sockaddr_in *);
		if (addr2->sin_family == AF_INET &&
		    addr1->sin_addr.s_addr == addr2->sin_addr.s_addr)
			return (1);
		break;
#ifdef INET6
	case AF_INET6:
		{
		struct sockaddr_in6 *inet6addr1, *inet6addr2;

		inet6addr1 = NFSSOCKADDR(nam1, struct sockaddr_in6 *);
		inet6addr2 = NFSSOCKADDR(nam2, struct sockaddr_in6 *);
		/* XXX - should test sin6_scope_id ? */
		if (inet6addr2->sin6_family == AF_INET6 &&
		    IN6_ARE_ADDR_EQUAL(&inet6addr1->sin6_addr,
			  &inet6addr2->sin6_addr))
			return (1);
		}
		break;
#endif
	}
	return (0);
}


/*
 * Trim the stuff already dissected off the mbuf list.
 */
APPLESTATIC void
newnfs_trimleading(nd)
	struct nfsrv_descript *nd;
{
	mbuf_t m, n;
	int offs;

	/*
	 * First, free up leading mbufs.
	 */
	if (nd->nd_mrep != nd->nd_md) {
		m = nd->nd_mrep;
		while (mbuf_next(m) != nd->nd_md) {
			if (mbuf_next(m) == NULL)
				panic("nfsm trim leading");
			m = mbuf_next(m);
		}
		mbuf_setnext(m, NULL);
		mbuf_freem(nd->nd_mrep);
	}
	m = nd->nd_md;

	/*
	 * Now, adjust this mbuf, based on nd_dpos.
	 */
	offs = nd->nd_dpos - NFSMTOD(m, caddr_t);
	if (offs == mbuf_len(m)) {
		n = m;
		m = mbuf_next(m);
		if (m == NULL)
			panic("nfsm trim leading2");
		mbuf_setnext(n, NULL);
		mbuf_freem(n);
	} else if (offs > 0) {
		mbuf_setlen(m, mbuf_len(m) - offs);
		NFSM_DATAP(m, offs);
	} else if (offs < 0)
		panic("nfsm trimleading offs");
	nd->nd_mrep = m;
	nd->nd_md = m;
	nd->nd_dpos = NFSMTOD(m, caddr_t);
}

/*
 * Trim trailing data off the mbuf list being built.
 */
APPLESTATIC void
newnfs_trimtrailing(nd, mb, bpos)
	struct nfsrv_descript *nd;
	mbuf_t mb;
	caddr_t bpos;
{

	if (mbuf_next(mb)) {
		mbuf_freem(mbuf_next(mb));
		mbuf_setnext(mb, NULL);
	}
	mbuf_setlen(mb, bpos - NFSMTOD(mb, caddr_t));
	nd->nd_mb = mb;
	nd->nd_bpos = bpos;
}

/*
 * Dissect a file handle on the client.
 */
APPLESTATIC int
nfsm_getfh(struct nfsrv_descript *nd, struct nfsfh **nfhpp)
{
	u_int32_t *tl;
	struct nfsfh *nfhp;
	int error, len;

	*nfhpp = NULL;
	if (nd->nd_flag & (ND_NFSV3 | ND_NFSV4)) {
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		if ((len = fxdr_unsigned(int, *tl)) <= 0 ||
			len > NFSX_FHMAX) {
			error = EBADRPC;
			goto nfsmout;
		}
	} else
		len = NFSX_V2FH;
	nfhp = malloc(sizeof (struct nfsfh) + len,
	    M_NFSFH, M_WAITOK);
	error = nfsrv_mtostr(nd, nfhp->nfh_fh, len);
	if (error) {
		free(nfhp, M_NFSFH);
		goto nfsmout;
	}
	nfhp->nfh_len = len;
	*nfhpp = nfhp;
nfsmout:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Break down the nfsv4 acl.
 * If the aclp == NULL or won't fit in an acl, just discard the acl info.
 */
APPLESTATIC int
nfsrv_dissectacl(struct nfsrv_descript *nd, NFSACL_T *aclp, int *aclerrp,
    int *aclsizep, __unused NFSPROC_T *p)
{
	u_int32_t *tl;
	int i, aclsize;
	int acecnt, error = 0, aceerr = 0, acesize;

	*aclerrp = 0;
	if (aclp)
		aclp->acl_cnt = 0;
	/*
	 * Parse out the ace entries and expect them to conform to
	 * what can be supported by R/W/X bits.
	 */
	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	aclsize = NFSX_UNSIGNED;
	acecnt = fxdr_unsigned(int, *tl);
	if (acecnt > ACL_MAX_ENTRIES)
		aceerr = NFSERR_ATTRNOTSUPP;
	if (nfsrv_useacl == 0)
		aceerr = NFSERR_ATTRNOTSUPP;
	for (i = 0; i < acecnt; i++) {
		if (aclp && !aceerr)
			error = nfsrv_dissectace(nd, &aclp->acl_entry[i],
			    &aceerr, &acesize, p);
		else
			error = nfsrv_skipace(nd, &acesize);
		if (error)
			goto nfsmout;
		aclsize += acesize;
	}
	if (aclp && !aceerr)
		aclp->acl_cnt = acecnt;
	if (aceerr)
		*aclerrp = aceerr;
	if (aclsizep)
		*aclsizep = aclsize;
nfsmout:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Skip over an NFSv4 ace entry. Just dissect the xdr and discard it.
 */
static int
nfsrv_skipace(struct nfsrv_descript *nd, int *acesizep)
{
	u_int32_t *tl;
	int error, len = 0;

	NFSM_DISSECT(tl, u_int32_t *, 4 * NFSX_UNSIGNED);
	len = fxdr_unsigned(int, *(tl + 3));
	error = nfsm_advance(nd, NFSM_RNDUP(len), -1);
nfsmout:
	*acesizep = NFSM_RNDUP(len) + (4 * NFSX_UNSIGNED);
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Get attribute bits from an mbuf list.
 * Returns EBADRPC for a parsing error, 0 otherwise.
 * If the clearinvalid flag is set, clear the bits not supported.
 */
APPLESTATIC int
nfsrv_getattrbits(struct nfsrv_descript *nd, nfsattrbit_t *attrbitp, int *cntp,
    int *retnotsupp)
{
	u_int32_t *tl;
	int cnt, i, outcnt;
	int error = 0;

	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	cnt = fxdr_unsigned(int, *tl);
	if (cnt < 0) {
		error = NFSERR_BADXDR;
		goto nfsmout;
	}
	if (cnt > NFSATTRBIT_MAXWORDS)
		outcnt = NFSATTRBIT_MAXWORDS;
	else
		outcnt = cnt;
	NFSZERO_ATTRBIT(attrbitp);
	if (outcnt > 0) {
		NFSM_DISSECT(tl, u_int32_t *, outcnt * NFSX_UNSIGNED);
		for (i = 0; i < outcnt; i++)
			attrbitp->bits[i] = fxdr_unsigned(u_int32_t, *tl++);
	}
	for (i = 0; i < (cnt - outcnt); i++) {
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		if (retnotsupp != NULL && *tl != 0)
			*retnotsupp = NFSERR_ATTRNOTSUPP;
	}
	if (cntp)
		*cntp = NFSX_UNSIGNED + (cnt * NFSX_UNSIGNED);
nfsmout:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Get the attributes for V4.
 * If the compare flag is true, test for any attribute changes,
 * otherwise return the attribute values.
 * These attributes cover fields in "struct vattr", "struct statfs",
 * "struct nfsfsinfo", the file handle and the lease duration.
 * The value of retcmpp is set to 1 if all attributes are the same,
 * and 0 otherwise.
 * Returns EBADRPC if it can't be parsed, 0 otherwise.
 */
APPLESTATIC int
nfsv4_loadattr(struct nfsrv_descript *nd, vnode_t vp,
    struct nfsvattr *nap, struct nfsfh **nfhpp, fhandle_t *fhp, int fhsize,
    struct nfsv3_pathconf *pc, struct statfs *sbp, struct nfsstatfs *sfp,
    struct nfsfsinfo *fsp, NFSACL_T *aclp, int compare, int *retcmpp,
    u_int32_t *leasep, u_int32_t *rderrp, NFSPROC_T *p, struct ucred *cred)
{
	u_int32_t *tl;
	int i = 0, j, k, l = 0, m, bitpos, attrsum = 0;
	int error, tfhsize, aceerr, attrsize, cnt, retnotsup;
	u_char *cp, *cp2, namestr[NFSV4_SMALLSTR + 1];
	nfsattrbit_t attrbits, retattrbits, checkattrbits;
	struct nfsfh *tnfhp;
	struct nfsreferral *refp;
	u_quad_t tquad;
	nfsquad_t tnfsquad;
	struct timespec temptime;
	uid_t uid;
	gid_t gid;
	u_int32_t freenum = 0, tuint;
	u_int64_t uquad = 0, thyp, thyp2;
#ifdef QUOTA
	struct dqblk dqb;
	uid_t savuid;
#endif

	CTASSERT(sizeof(ino_t) == sizeof(uint64_t));
	if (compare) {
		retnotsup = 0;
		error = nfsrv_getattrbits(nd, &attrbits, NULL, &retnotsup);
	} else {
		error = nfsrv_getattrbits(nd, &attrbits, NULL, NULL);
	}
	if (error)
		goto nfsmout;

	if (compare) {
		*retcmpp = retnotsup;
	} else {
		/*
		 * Just set default values to some of the important ones.
		 */
		if (nap != NULL) {
			nap->na_type = VREG;
			nap->na_mode = 0;
			nap->na_rdev = (NFSDEV_T)0;
			nap->na_mtime.tv_sec = 0;
			nap->na_mtime.tv_nsec = 0;
			nap->na_gen = 0;
			nap->na_flags = 0;
			nap->na_blocksize = NFS_FABLKSIZE;
		}
		if (sbp != NULL) {
			sbp->f_bsize = NFS_FABLKSIZE;
			sbp->f_blocks = 0;
			sbp->f_bfree = 0;
			sbp->f_bavail = 0;
			sbp->f_files = 0;
			sbp->f_ffree = 0;
		}
		if (fsp != NULL) {
			fsp->fs_rtmax = 8192;
			fsp->fs_rtpref = 8192;
			fsp->fs_maxname = NFS_MAXNAMLEN;
			fsp->fs_wtmax = 8192;
			fsp->fs_wtpref = 8192;
			fsp->fs_wtmult = NFS_FABLKSIZE;
			fsp->fs_dtpref = 8192;
			fsp->fs_maxfilesize = 0xffffffffffffffffull;
			fsp->fs_timedelta.tv_sec = 0;
			fsp->fs_timedelta.tv_nsec = 1;
			fsp->fs_properties = (NFSV3_FSFLINK | NFSV3_FSFSYMLINK |
				NFSV3_FSFHOMOGENEOUS | NFSV3_FSFCANSETTIME);
		}
		if (pc != NULL) {
			pc->pc_linkmax = NFS_LINK_MAX;
			pc->pc_namemax = NAME_MAX;
			pc->pc_notrunc = 0;
			pc->pc_chownrestricted = 0;
			pc->pc_caseinsensitive = 0;
			pc->pc_casepreserving = 1;
		}
		if (sfp != NULL) {
			sfp->sf_ffiles = UINT64_MAX;
			sfp->sf_tfiles = UINT64_MAX;
			sfp->sf_afiles = UINT64_MAX;
			sfp->sf_fbytes = UINT64_MAX;
			sfp->sf_tbytes = UINT64_MAX;
			sfp->sf_abytes = UINT64_MAX;
		}
	}

	/*
	 * Loop around getting the attributes.
	 */
	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	attrsize = fxdr_unsigned(int, *tl);
	for (bitpos = 0; bitpos < NFSATTRBIT_MAX; bitpos++) {
	    if (attrsum > attrsize) {
		error = NFSERR_BADXDR;
		goto nfsmout;
	    }
	    if (NFSISSET_ATTRBIT(&attrbits, bitpos))
		switch (bitpos) {
		case NFSATTRBIT_SUPPORTEDATTRS:
			retnotsup = 0;
			if (compare || nap == NULL)
			    error = nfsrv_getattrbits(nd, &retattrbits,
				&cnt, &retnotsup);
			else
			    error = nfsrv_getattrbits(nd, &nap->na_suppattr,
				&cnt, &retnotsup);
			if (error)
			    goto nfsmout;
			if (compare && !(*retcmpp)) {
			   NFSSETSUPP_ATTRBIT(&checkattrbits);

			   /* Some filesystem do not support NFSv4ACL   */
			   if (nfsrv_useacl == 0 || nfs_supportsnfsv4acls(vp) == 0) {
				NFSCLRBIT_ATTRBIT(&checkattrbits, NFSATTRBIT_ACL);
				NFSCLRBIT_ATTRBIT(&checkattrbits, NFSATTRBIT_ACLSUPPORT);
		   	   }
			   if (!NFSEQUAL_ATTRBIT(&retattrbits, &checkattrbits)
			       || retnotsup)
				*retcmpp = NFSERR_NOTSAME;
			}
			attrsum += cnt;
			break;
		case NFSATTRBIT_TYPE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (nap->na_type != nfsv34tov_type(*tl))
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (nap != NULL) {
				nap->na_type = nfsv34tov_type(*tl);
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_FHEXPIRETYPE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare && !(*retcmpp)) {
				if (fxdr_unsigned(int, *tl) !=
					NFSV4FHTYPE_PERSISTENT)
					*retcmpp = NFSERR_NOTSAME;
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CHANGE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp)) {
				    if (nap->na_filerev != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (nap != NULL) {
				nap->na_filerev = fxdr_hyper(tl);
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SIZE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp)) {
				    if (nap->na_size != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (nap != NULL) {
				nap->na_size = fxdr_hyper(tl);
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_LINKSUPPORT:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (fsp->fs_properties & NFSV3_FSFLINK) {
					if (*tl == newnfs_false)
						*retcmpp = NFSERR_NOTSAME;
				    } else {
					if (*tl == newnfs_true)
						*retcmpp = NFSERR_NOTSAME;
				    }
				}
			} else if (fsp != NULL) {
				if (*tl == newnfs_true)
					fsp->fs_properties |= NFSV3_FSFLINK;
				else
					fsp->fs_properties &= ~NFSV3_FSFLINK;
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_SYMLINKSUPPORT:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (fsp->fs_properties & NFSV3_FSFSYMLINK) {
					if (*tl == newnfs_false)
						*retcmpp = NFSERR_NOTSAME;
				    } else {
					if (*tl == newnfs_true)
						*retcmpp = NFSERR_NOTSAME;
				    }
				}
			} else if (fsp != NULL) {
				if (*tl == newnfs_true)
					fsp->fs_properties |= NFSV3_FSFSYMLINK;
				else
					fsp->fs_properties &= ~NFSV3_FSFSYMLINK;
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_NAMEDATTR:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare && !(*retcmpp)) {
				if (*tl != newnfs_false)
					*retcmpp = NFSERR_NOTSAME;
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_FSID:
			NFSM_DISSECT(tl, u_int32_t *, 4 * NFSX_UNSIGNED);
			thyp = fxdr_hyper(tl);
			tl += 2;
			thyp2 = fxdr_hyper(tl);
			if (compare) {
			    if (*retcmpp == 0) {
				if (thyp != (u_int64_t)
				    vfs_statfs(vnode_mount(vp))->f_fsid.val[0] ||
				    thyp2 != (u_int64_t)
				    vfs_statfs(vnode_mount(vp))->f_fsid.val[1])
					*retcmpp = NFSERR_NOTSAME;
			    }
			} else if (nap != NULL) {
				nap->na_filesid[0] = thyp;
				nap->na_filesid[1] = thyp2;
			}
			attrsum += (4 * NFSX_UNSIGNED);
			break;
		case NFSATTRBIT_UNIQUEHANDLES:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare && !(*retcmpp)) {
				if (*tl != newnfs_true)
					*retcmpp = NFSERR_NOTSAME;
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_LEASETIME:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (fxdr_unsigned(int, *tl) != nfsrv_lease &&
				    !(*retcmpp))
					*retcmpp = NFSERR_NOTSAME;
			} else if (leasep != NULL) {
				*leasep = fxdr_unsigned(u_int32_t, *tl);
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_RDATTRERROR:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				 if (!(*retcmpp))
					*retcmpp = NFSERR_INVAL;
			} else if (rderrp != NULL) {
				*rderrp = fxdr_unsigned(u_int32_t, *tl);
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_ACL:
			if (compare) {
			  if (!(*retcmpp)) {
			    if (nfsrv_useacl && nfs_supportsnfsv4acls(vp)) {
				NFSACL_T *naclp;

				naclp = acl_alloc(M_WAITOK);
				error = nfsrv_dissectacl(nd, naclp, &aceerr,
				    &cnt, p);
				if (error) {
				    acl_free(naclp);
				    goto nfsmout;
				}
				if (aceerr || aclp == NULL ||
				    nfsrv_compareacl(aclp, naclp))
				    *retcmpp = NFSERR_NOTSAME;
				acl_free(naclp);
			    } else {
				error = nfsrv_dissectacl(nd, NULL, &aceerr,
				    &cnt, p);
				*retcmpp = NFSERR_ATTRNOTSUPP;
			    }
			  }
			} else {
				if (vp != NULL && aclp != NULL)
				    error = nfsrv_dissectacl(nd, aclp, &aceerr,
					&cnt, p);
				else
				    error = nfsrv_dissectacl(nd, NULL, &aceerr,
					&cnt, p);
				if (error)
				    goto nfsmout;
			}
			
			attrsum += cnt;
			break;
		case NFSATTRBIT_ACLSUPPORT:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare && !(*retcmpp)) {
				if (nfsrv_useacl && nfs_supportsnfsv4acls(vp)) {
					if (fxdr_unsigned(u_int32_t, *tl) !=
					    NFSV4ACE_SUPTYPES)
						*retcmpp = NFSERR_NOTSAME;
				} else {
					*retcmpp = NFSERR_ATTRNOTSUPP;
				}
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_ARCHIVE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare && !(*retcmpp))
				*retcmpp = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CANSETTIME:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (fsp->fs_properties & NFSV3_FSFCANSETTIME) {
					if (*tl == newnfs_false)
						*retcmpp = NFSERR_NOTSAME;
				    } else {
					if (*tl == newnfs_true)
						*retcmpp = NFSERR_NOTSAME;
				    }
				}
			} else if (fsp != NULL) {
				if (*tl == newnfs_true)
					fsp->fs_properties |= NFSV3_FSFCANSETTIME;
				else
					fsp->fs_properties &= ~NFSV3_FSFCANSETTIME;
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CASEINSENSITIVE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (*tl != newnfs_false)
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (pc != NULL) {
				pc->pc_caseinsensitive =
				    fxdr_unsigned(u_int32_t, *tl);
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CASEPRESERVING:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (*tl != newnfs_true)
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (pc != NULL) {
				pc->pc_casepreserving =
				    fxdr_unsigned(u_int32_t, *tl);
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CHOWNRESTRICTED:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (*tl != newnfs_true)
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (pc != NULL) {
				pc->pc_chownrestricted =
				    fxdr_unsigned(u_int32_t, *tl);
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_FILEHANDLE:
			error = nfsm_getfh(nd, &tnfhp);
			if (error)
				goto nfsmout;
			tfhsize = tnfhp->nfh_len;
			if (compare) {
				if (!(*retcmpp) &&
				    !NFSRV_CMPFH(tnfhp->nfh_fh, tfhsize,
				     fhp, fhsize))
					*retcmpp = NFSERR_NOTSAME;
				free(tnfhp, M_NFSFH);
			} else if (nfhpp != NULL) {
				*nfhpp = tnfhp;
			} else {
				free(tnfhp, M_NFSFH);
			}
			attrsum += (NFSX_UNSIGNED + NFSM_RNDUP(tfhsize));
			break;
		case NFSATTRBIT_FILEID:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			thyp = fxdr_hyper(tl);
			if (compare) {
				if (!(*retcmpp)) {
					if (nap->na_fileid != thyp)
						*retcmpp = NFSERR_NOTSAME;
				}
			} else if (nap != NULL)
				nap->na_fileid = thyp;
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_FILESAVAIL:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp) &&
				    sfp->sf_afiles != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
			} else if (sfp != NULL) {
				sfp->sf_afiles = fxdr_hyper(tl);
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_FILESFREE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp) &&
				    sfp->sf_ffiles != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
			} else if (sfp != NULL) {
				sfp->sf_ffiles = fxdr_hyper(tl);
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_FILESTOTAL:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp) &&
				    sfp->sf_tfiles != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
			} else if (sfp != NULL) {
				sfp->sf_tfiles = fxdr_hyper(tl);
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_FSLOCATIONS:
			error = nfsrv_getrefstr(nd, &cp, &cp2, &l, &m);
			if (error)
				goto nfsmout;
			attrsum += l;
			if (compare && !(*retcmpp)) {
				refp = nfsv4root_getreferral(vp, NULL, 0);
				if (refp != NULL) {
					if (cp == NULL || cp2 == NULL ||
					    strcmp(cp, "/") ||
					    strcmp(cp2, refp->nfr_srvlist))
						*retcmpp = NFSERR_NOTSAME;
				} else if (m == 0) {
					*retcmpp = NFSERR_NOTSAME;
				}
			}
			if (cp != NULL)
				free(cp, M_NFSSTRING);
			if (cp2 != NULL)
				free(cp2, M_NFSSTRING);
			break;
		case NFSATTRBIT_HIDDEN:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare && !(*retcmpp))
				*retcmpp = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_HOMOGENEOUS:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (fsp->fs_properties &
					NFSV3_FSFHOMOGENEOUS) {
					if (*tl == newnfs_false)
						*retcmpp = NFSERR_NOTSAME;
				    } else {
					if (*tl == newnfs_true)
						*retcmpp = NFSERR_NOTSAME;
				    }
				}
			} else if (fsp != NULL) {
				if (*tl == newnfs_true)
				    fsp->fs_properties |= NFSV3_FSFHOMOGENEOUS;
				else
				    fsp->fs_properties &= ~NFSV3_FSFHOMOGENEOUS;
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_MAXFILESIZE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			tnfsquad.qval = fxdr_hyper(tl);
			if (compare) {
				if (!(*retcmpp)) {
					tquad = NFSRV_MAXFILESIZE;
					if (tquad != tnfsquad.qval)
						*retcmpp = NFSERR_NOTSAME;
				}
			} else if (fsp != NULL) {
				fsp->fs_maxfilesize = tnfsquad.qval;
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_MAXLINK:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (fxdr_unsigned(int, *tl) != NFS_LINK_MAX)
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (pc != NULL) {
				pc->pc_linkmax = fxdr_unsigned(u_int32_t, *tl);
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_MAXNAME:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (fsp->fs_maxname !=
					fxdr_unsigned(u_int32_t, *tl))
						*retcmpp = NFSERR_NOTSAME;
				}
			} else {
				tuint = fxdr_unsigned(u_int32_t, *tl);
				/*
				 * Some Linux NFSv4 servers report this
				 * as 0 or 4billion, so I'll set it to
				 * NFS_MAXNAMLEN. If a server actually creates
				 * a name longer than NFS_MAXNAMLEN, it will
				 * get an error back.
				 */
				if (tuint == 0 || tuint > NFS_MAXNAMLEN)
					tuint = NFS_MAXNAMLEN;
				if (fsp != NULL)
					fsp->fs_maxname = tuint;
				if (pc != NULL)
					pc->pc_namemax = tuint;
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_MAXREAD:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp)) {
				    if (fsp->fs_rtmax != fxdr_unsigned(u_int32_t,
					*(tl + 1)) || *tl != 0)
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (fsp != NULL) {
				fsp->fs_rtmax = fxdr_unsigned(u_int32_t, *++tl);
				fsp->fs_rtpref = fsp->fs_rtmax;
				fsp->fs_dtpref = fsp->fs_rtpref;
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_MAXWRITE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp)) {
				    if (fsp->fs_wtmax != fxdr_unsigned(u_int32_t,
					*(tl + 1)) || *tl != 0)
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (fsp != NULL) {
				fsp->fs_wtmax = fxdr_unsigned(int, *++tl);
				fsp->fs_wtpref = fsp->fs_wtmax;
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_MIMETYPE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			i = fxdr_unsigned(int, *tl);
			attrsum += (NFSX_UNSIGNED + NFSM_RNDUP(i));
			error = nfsm_advance(nd, NFSM_RNDUP(i), -1);
			if (error)
				goto nfsmout;
			if (compare && !(*retcmpp))
				*retcmpp = NFSERR_ATTRNOTSUPP;
			break;
		case NFSATTRBIT_MODE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (nap->na_mode != nfstov_mode(*tl))
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (nap != NULL) {
				nap->na_mode = nfstov_mode(*tl);
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_NOTRUNC:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare) {
				if (!(*retcmpp)) {
				    if (*tl != newnfs_true)
					*retcmpp = NFSERR_NOTSAME;
				}
			} else if (pc != NULL) {
				pc->pc_notrunc = fxdr_unsigned(u_int32_t, *tl);
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_NUMLINKS:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			tuint = fxdr_unsigned(u_int32_t, *tl);
			if (compare) {
			    if (!(*retcmpp)) {
				if ((u_int32_t)nap->na_nlink != tuint)
					*retcmpp = NFSERR_NOTSAME;
			    }
			} else if (nap != NULL) {
				nap->na_nlink = tuint;
			}
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_OWNER:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			j = fxdr_unsigned(int, *tl);
			if (j < 0) {
				error = NFSERR_BADXDR;
				goto nfsmout;
			}
			attrsum += (NFSX_UNSIGNED + NFSM_RNDUP(j));
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
			if (compare) {
			    if (!(*retcmpp)) {
				if (nfsv4_strtouid(nd, cp, j, &uid) ||
				    nap->na_uid != uid)
				    *retcmpp = NFSERR_NOTSAME;
			    }
			} else if (nap != NULL) {
				if (nfsv4_strtouid(nd, cp, j, &uid))
					nap->na_uid = nfsrv_defaultuid;
				else
					nap->na_uid = uid;
			}
			if (j > NFSV4_SMALLSTR)
				free(cp, M_NFSSTRING);
			break;
		case NFSATTRBIT_OWNERGROUP:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			j = fxdr_unsigned(int, *tl);
			if (j < 0) {
				error =  NFSERR_BADXDR;
				goto nfsmout;
			}
			attrsum += (NFSX_UNSIGNED + NFSM_RNDUP(j));
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
			if (compare) {
			    if (!(*retcmpp)) {
				if (nfsv4_strtogid(nd, cp, j, &gid) ||
				    nap->na_gid != gid)
				    *retcmpp = NFSERR_NOTSAME;
			    }
			} else if (nap != NULL) {
				if (nfsv4_strtogid(nd, cp, j, &gid))
					nap->na_gid = nfsrv_defaultgid;
				else
					nap->na_gid = gid;
			}
			if (j > NFSV4_SMALLSTR)
				free(cp, M_NFSSTRING);
			break;
		case NFSATTRBIT_QUOTAHARD:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (sbp != NULL) {
			    if (priv_check_cred(cred, PRIV_VFS_EXCEEDQUOTA))
				freenum = sbp->f_bfree;
			    else
				freenum = sbp->f_bavail;
#ifdef QUOTA
			    /*
			     * ufs_quotactl() insists that the uid argument
			     * equal p_ruid for non-root quota access, so
			     * we'll just make sure that's the case.
			     */
			    savuid = p->p_cred->p_ruid;
			    p->p_cred->p_ruid = cred->cr_uid;
			    if (!VFS_QUOTACTL(vnode_mount(vp),QCMD(Q_GETQUOTA,
				USRQUOTA), cred->cr_uid, (caddr_t)&dqb))
				freenum = min(dqb.dqb_bhardlimit, freenum);
			    p->p_cred->p_ruid = savuid;
#endif	/* QUOTA */
			    uquad = (u_int64_t)freenum;
			    NFSQUOTABLKTOBYTE(uquad, sbp->f_bsize);
			}
			if (compare && !(*retcmpp)) {
				if (uquad != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_QUOTASOFT:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (sbp != NULL) {
			    if (priv_check_cred(cred, PRIV_VFS_EXCEEDQUOTA))
				freenum = sbp->f_bfree;
			    else
				freenum = sbp->f_bavail;
#ifdef QUOTA
			    /*
			     * ufs_quotactl() insists that the uid argument
			     * equal p_ruid for non-root quota access, so
			     * we'll just make sure that's the case.
			     */
			    savuid = p->p_cred->p_ruid;
			    p->p_cred->p_ruid = cred->cr_uid;
			    if (!VFS_QUOTACTL(vnode_mount(vp),QCMD(Q_GETQUOTA,
				USRQUOTA), cred->cr_uid, (caddr_t)&dqb))
				freenum = min(dqb.dqb_bsoftlimit, freenum);
			    p->p_cred->p_ruid = savuid;
#endif	/* QUOTA */
			    uquad = (u_int64_t)freenum;
			    NFSQUOTABLKTOBYTE(uquad, sbp->f_bsize);
			}
			if (compare && !(*retcmpp)) {
				if (uquad != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_QUOTAUSED:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (sbp != NULL) {
			    freenum = 0;
#ifdef QUOTA
			    /*
			     * ufs_quotactl() insists that the uid argument
			     * equal p_ruid for non-root quota access, so
			     * we'll just make sure that's the case.
			     */
			    savuid = p->p_cred->p_ruid;
			    p->p_cred->p_ruid = cred->cr_uid;
			    if (!VFS_QUOTACTL(vnode_mount(vp),QCMD(Q_GETQUOTA,
				USRQUOTA), cred->cr_uid, (caddr_t)&dqb))
				freenum = dqb.dqb_curblocks;
			    p->p_cred->p_ruid = savuid;
#endif	/* QUOTA */
			    uquad = (u_int64_t)freenum;
			    NFSQUOTABLKTOBYTE(uquad, sbp->f_bsize);
			}
			if (compare && !(*retcmpp)) {
				if (uquad != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_RAWDEV:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_V4SPECDATA);
			j = fxdr_unsigned(int, *tl++);
			k = fxdr_unsigned(int, *tl);
			if (compare) {
			    if (!(*retcmpp)) {
				if (nap->na_rdev != NFSMAKEDEV(j, k))
					*retcmpp = NFSERR_NOTSAME;
			    }
			} else if (nap != NULL) {
				nap->na_rdev = NFSMAKEDEV(j, k);
			}
			attrsum += NFSX_V4SPECDATA;
			break;
		case NFSATTRBIT_SPACEAVAIL:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp) &&
				    sfp->sf_abytes != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
			} else if (sfp != NULL) {
				sfp->sf_abytes = fxdr_hyper(tl);
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SPACEFREE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp) &&
				    sfp->sf_fbytes != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
			} else if (sfp != NULL) {
				sfp->sf_fbytes = fxdr_hyper(tl);
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SPACETOTAL:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			if (compare) {
				if (!(*retcmpp) &&
				    sfp->sf_tbytes != fxdr_hyper(tl))
					*retcmpp = NFSERR_NOTSAME;
			} else if (sfp != NULL) {
				sfp->sf_tbytes = fxdr_hyper(tl);
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SPACEUSED:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			thyp = fxdr_hyper(tl);
			if (compare) {
			    if (!(*retcmpp)) {
				if ((u_int64_t)nap->na_bytes != thyp)
					*retcmpp = NFSERR_NOTSAME;
			    }
			} else if (nap != NULL) {
				nap->na_bytes = thyp;
			}
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SYSTEM:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			if (compare && !(*retcmpp))
				*retcmpp = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_TIMEACCESS:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			fxdr_nfsv4time(tl, &temptime);
			if (compare) {
			    if (!(*retcmpp)) {
				if (!NFS_CMPTIME(temptime, nap->na_atime))
					*retcmpp = NFSERR_NOTSAME;
			    }
			} else if (nap != NULL) {
				nap->na_atime = temptime;
			}
			attrsum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEACCESSSET:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			attrsum += NFSX_UNSIGNED;
			i = fxdr_unsigned(int, *tl);
			if (i == NFSV4SATTRTIME_TOCLIENT) {
				NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
				attrsum += NFSX_V4TIME;
			}
			if (compare && !(*retcmpp))
				*retcmpp = NFSERR_INVAL;
			break;
		case NFSATTRBIT_TIMEBACKUP:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			if (compare && !(*retcmpp))
				*retcmpp = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMECREATE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			if (compare && !(*retcmpp))
				*retcmpp = NFSERR_ATTRNOTSUPP;
			attrsum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEDELTA:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			if (fsp != NULL) {
			    if (compare) {
				if (!(*retcmpp)) {
				    if ((u_int32_t)fsp->fs_timedelta.tv_sec !=
					fxdr_unsigned(u_int32_t, *(tl + 1)) ||
				        (u_int32_t)fsp->fs_timedelta.tv_nsec !=
					(fxdr_unsigned(u_int32_t, *(tl + 2)) %
					 1000000000) ||
					*tl != 0)
					    *retcmpp = NFSERR_NOTSAME;
				}
			    } else {
				fxdr_nfsv4time(tl, &fsp->fs_timedelta);
			    }
			}
			attrsum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEMETADATA:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			fxdr_nfsv4time(tl, &temptime);
			if (compare) {
			    if (!(*retcmpp)) {
				if (!NFS_CMPTIME(temptime, nap->na_ctime))
					*retcmpp = NFSERR_NOTSAME;
			    }
			} else if (nap != NULL) {
				nap->na_ctime = temptime;
			}
			attrsum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEMODIFY:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
			fxdr_nfsv4time(tl, &temptime);
			if (compare) {
			    if (!(*retcmpp)) {
				if (!NFS_CMPTIME(temptime, nap->na_mtime))
					*retcmpp = NFSERR_NOTSAME;
			    }
			} else if (nap != NULL) {
				nap->na_mtime = temptime;
			}
			attrsum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEMODIFYSET:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			attrsum += NFSX_UNSIGNED;
			i = fxdr_unsigned(int, *tl);
			if (i == NFSV4SATTRTIME_TOCLIENT) {
				NFSM_DISSECT(tl, u_int32_t *, NFSX_V4TIME);
				attrsum += NFSX_V4TIME;
			}
			if (compare && !(*retcmpp))
				*retcmpp = NFSERR_INVAL;
			break;
		case NFSATTRBIT_MOUNTEDONFILEID:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_HYPER);
			thyp = fxdr_hyper(tl);
			if (compare) {
				if (!(*retcmpp)) {
					if (!vp || !nfsrv_atroot(vp, &thyp2))
						thyp2 = nap->na_fileid;
					if (thyp2 != thyp)
						*retcmpp = NFSERR_NOTSAME;
				}
			} else if (nap != NULL)
				nap->na_mntonfileno = thyp;
			attrsum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SUPPATTREXCLCREAT:
			retnotsup = 0;
			error = nfsrv_getattrbits(nd, &retattrbits,
			    &cnt, &retnotsup);
			if (error)
			    goto nfsmout;
			if (compare && !(*retcmpp)) {
			   NFSSETSUPP_ATTRBIT(&checkattrbits);
			   NFSCLRNOTSETABLE_ATTRBIT(&checkattrbits);
			   NFSCLRBIT_ATTRBIT(&checkattrbits,
				NFSATTRBIT_TIMEACCESSSET);
			   if (!NFSEQUAL_ATTRBIT(&retattrbits, &checkattrbits)
			       || retnotsup)
				*retcmpp = NFSERR_NOTSAME;
			}
			attrsum += cnt;
			break;
		case NFSATTRBIT_FSLAYOUTTYPE:
		case NFSATTRBIT_LAYOUTTYPE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			attrsum += NFSX_UNSIGNED;
			i = fxdr_unsigned(int, *tl);
			if (i > 0) {
				NFSM_DISSECT(tl, u_int32_t *, i *
				    NFSX_UNSIGNED);
				attrsum += i * NFSX_UNSIGNED;
				j = fxdr_unsigned(int, *tl);
				if (i == 1 && compare && !(*retcmpp) &&
				    (((nfsrv_doflexfile != 0 ||
				       nfsrv_maxpnfsmirror > 1) &&
				      j != NFSLAYOUT_FLEXFILE) ||
				    (nfsrv_doflexfile == 0 &&
				     j != NFSLAYOUT_NFSV4_1_FILES)))
					*retcmpp = NFSERR_NOTSAME;
			}
			if (nfsrv_devidcnt == 0) {
				if (compare && !(*retcmpp) && i > 0)
					*retcmpp = NFSERR_NOTSAME;
			} else {
				if (compare && !(*retcmpp) && i != 1)
					*retcmpp = NFSERR_NOTSAME;
			}
			break;
		case NFSATTRBIT_LAYOUTALIGNMENT:
		case NFSATTRBIT_LAYOUTBLKSIZE:
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			attrsum += NFSX_UNSIGNED;
			i = fxdr_unsigned(int, *tl);
			if (compare && !(*retcmpp) && i != NFS_SRVMAXIO)
				*retcmpp = NFSERR_NOTSAME;
			break;
		default:
			printf("EEK! nfsv4_loadattr unknown attr=%d\n",
				bitpos);
			if (compare && !(*retcmpp))
				*retcmpp = NFSERR_ATTRNOTSUPP;
			/*
			 * and get out of the loop, since we can't parse
			 * the unknown attrbute data.
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
 * Implement sleep locks for newnfs. The nfslock_usecnt allows for a
 * shared lock and the NFSXXX_LOCK flag permits an exclusive lock.
 * The first argument is a pointer to an nfsv4lock structure.
 * The second argument is 1 iff a blocking lock is wanted.
 * If this argument is 0, the call waits until no thread either wants nor
 * holds an exclusive lock.
 * It returns 1 if the lock was acquired, 0 otherwise.
 * If several processes call this function concurrently wanting the exclusive
 * lock, one will get the lock and the rest will return without getting the
 * lock. (If the caller must have the lock, it simply calls this function in a
 *  loop until the function returns 1 to indicate the lock was acquired.)
 * Any usecnt must be decremented by calling nfsv4_relref() before
 * calling nfsv4_lock(). It was done this way, so nfsv4_lock() could
 * be called in a loop.
 * The isleptp argument is set to indicate if the call slept, iff not NULL
 * and the mp argument indicates to check for a forced dismount, iff not
 * NULL.
 */
APPLESTATIC int
nfsv4_lock(struct nfsv4lock *lp, int iwantlock, int *isleptp,
    void *mutex, struct mount *mp)
{

	if (isleptp)
		*isleptp = 0;
	/*
	 * If a lock is wanted, loop around until the lock is acquired by
	 * someone and then released. If I want the lock, try to acquire it.
	 * For a lock to be issued, no lock must be in force and the usecnt
	 * must be zero.
	 */
	if (iwantlock) {
	    if (!(lp->nfslock_lock & NFSV4LOCK_LOCK) &&
		lp->nfslock_usecnt == 0) {
		lp->nfslock_lock &= ~NFSV4LOCK_LOCKWANTED;
		lp->nfslock_lock |= NFSV4LOCK_LOCK;
		return (1);
	    }
	    lp->nfslock_lock |= NFSV4LOCK_LOCKWANTED;
	}
	while (lp->nfslock_lock & (NFSV4LOCK_LOCK | NFSV4LOCK_LOCKWANTED)) {
		if (mp != NULL && NFSCL_FORCEDISM(mp)) {
			lp->nfslock_lock &= ~NFSV4LOCK_LOCKWANTED;
			return (0);
		}
		lp->nfslock_lock |= NFSV4LOCK_WANTED;
		if (isleptp)
			*isleptp = 1;
		(void) nfsmsleep(&lp->nfslock_lock, mutex,
		    PZERO - 1, "nfsv4lck", NULL);
		if (iwantlock && !(lp->nfslock_lock & NFSV4LOCK_LOCK) &&
		    lp->nfslock_usecnt == 0) {
			lp->nfslock_lock &= ~NFSV4LOCK_LOCKWANTED;
			lp->nfslock_lock |= NFSV4LOCK_LOCK;
			return (1);
		}
	}
	return (0);
}

/*
 * Release the lock acquired by nfsv4_lock().
 * The second argument is set to 1 to indicate the nfslock_usecnt should be
 * incremented, as well.
 */
APPLESTATIC void
nfsv4_unlock(struct nfsv4lock *lp, int incref)
{

	lp->nfslock_lock &= ~NFSV4LOCK_LOCK;
	if (incref)
		lp->nfslock_usecnt++;
	nfsv4_wanted(lp);
}

/*
 * Release a reference cnt.
 */
APPLESTATIC void
nfsv4_relref(struct nfsv4lock *lp)
{

	if (lp->nfslock_usecnt <= 0)
		panic("nfsv4root ref cnt");
	lp->nfslock_usecnt--;
	if (lp->nfslock_usecnt == 0)
		nfsv4_wanted(lp);
}

/*
 * Get a reference cnt.
 * This function will wait for any exclusive lock to be released, but will
 * not wait for threads that want the exclusive lock. If priority needs
 * to be given to threads that need the exclusive lock, a call to nfsv4_lock()
 * with the 2nd argument == 0 should be done before calling nfsv4_getref().
 * If the mp argument is not NULL, check for NFSCL_FORCEDISM() being set and
 * return without getting a refcnt for that case.
 */
APPLESTATIC void
nfsv4_getref(struct nfsv4lock *lp, int *isleptp, void *mutex,
    struct mount *mp)
{

	if (isleptp)
		*isleptp = 0;

	/*
	 * Wait for a lock held.
	 */
	while (lp->nfslock_lock & NFSV4LOCK_LOCK) {
		if (mp != NULL && NFSCL_FORCEDISM(mp))
			return;
		lp->nfslock_lock |= NFSV4LOCK_WANTED;
		if (isleptp)
			*isleptp = 1;
		(void) nfsmsleep(&lp->nfslock_lock, mutex,
		    PZERO - 1, "nfsv4gr", NULL);
	}
	if (mp != NULL && NFSCL_FORCEDISM(mp))
		return;

	lp->nfslock_usecnt++;
}

/*
 * Get a reference as above, but return failure instead of sleeping if
 * an exclusive lock is held.
 */
APPLESTATIC int
nfsv4_getref_nonblock(struct nfsv4lock *lp)
{

	if ((lp->nfslock_lock & NFSV4LOCK_LOCK) != 0)
		return (0);

	lp->nfslock_usecnt++;
	return (1);
}

/*
 * Test for a lock. Return 1 if locked, 0 otherwise.
 */
APPLESTATIC int
nfsv4_testlock(struct nfsv4lock *lp)
{

	if ((lp->nfslock_lock & NFSV4LOCK_LOCK) == 0 &&
	    lp->nfslock_usecnt == 0)
		return (0);
	return (1);
}

/*
 * Wake up anyone sleeping, waiting for this lock.
 */
static void
nfsv4_wanted(struct nfsv4lock *lp)
{

	if (lp->nfslock_lock & NFSV4LOCK_WANTED) {
		lp->nfslock_lock &= ~NFSV4LOCK_WANTED;
		wakeup((caddr_t)&lp->nfslock_lock);
	}
}

/*
 * Copy a string from an mbuf list into a character array.
 * Return EBADRPC if there is an mbuf error,
 * 0 otherwise.
 */
APPLESTATIC int
nfsrv_mtostr(struct nfsrv_descript *nd, char *str, int siz)
{
	char *cp;
	int xfer, len;
	mbuf_t mp;
	int rem, error = 0;

	mp = nd->nd_md;
	cp = nd->nd_dpos;
	len = NFSMTOD(mp, caddr_t) + mbuf_len(mp) - cp;
	rem = NFSM_RNDUP(siz) - siz;
	while (siz > 0) {
		if (len > siz)
			xfer = siz;
		else
			xfer = len;
		NFSBCOPY(cp, str, xfer);
		str += xfer;
		siz -= xfer;
		if (siz > 0) {
			mp = mbuf_next(mp);
			if (mp == NULL) {
				error = EBADRPC;
				goto out;
			}
			cp = NFSMTOD(mp, caddr_t);
			len = mbuf_len(mp);
		} else {
			cp += xfer;
			len -= xfer;
		}
	}
	*str = '\0';
	nd->nd_dpos = cp;
	nd->nd_md = mp;
	if (rem > 0) {
		if (len < rem)
			error = nfsm_advance(nd, rem, len);
		else
			nd->nd_dpos += rem;
	}

out:
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Fill in the attributes as marked by the bitmap (V4).
 */
APPLESTATIC int
nfsv4_fillattr(struct nfsrv_descript *nd, struct mount *mp, vnode_t vp,
    NFSACL_T *saclp, struct vattr *vap, fhandle_t *fhp, int rderror,
    nfsattrbit_t *attrbitp, struct ucred *cred, NFSPROC_T *p, int isdgram,
    int reterr, int supports_nfsv4acls, int at_root, uint64_t mounted_on_fileno,
    struct statfs *pnfssf)
{
	int bitpos, retnum = 0;
	u_int32_t *tl;
	int siz, prefixnum, error;
	u_char *cp, namestr[NFSV4_SMALLSTR];
	nfsattrbit_t attrbits, retbits;
	nfsattrbit_t *retbitp = &retbits;
	u_int32_t freenum, *retnump;
	u_int64_t uquad;
	struct statfs *fs;
	struct nfsfsinfo fsinf;
	struct timespec temptime;
	NFSACL_T *aclp, *naclp = NULL;
#ifdef QUOTA
	struct dqblk dqb;
	uid_t savuid;
#endif

	/*
	 * First, set the bits that can be filled and get fsinfo.
	 */
	NFSSET_ATTRBIT(retbitp, attrbitp);
	/*
	 * If both p and cred are NULL, it is a client side setattr call.
	 * If both p and cred are not NULL, it is a server side reply call.
	 * If p is not NULL and cred is NULL, it is a client side callback
	 * reply call.
	 */
	if (p == NULL && cred == NULL) {
		NFSCLRNOTSETABLE_ATTRBIT(retbitp);
		aclp = saclp;
	} else {
		NFSCLRNOTFILLABLE_ATTRBIT(retbitp);
		naclp = acl_alloc(M_WAITOK);
		aclp = naclp;
	}
	nfsvno_getfs(&fsinf, isdgram);
#ifndef APPLE
	/*
	 * Get the VFS_STATFS(), since some attributes need them.
	 */
	fs = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK);
	if (NFSISSETSTATFS_ATTRBIT(retbitp)) {
		error = VFS_STATFS(mp, fs);
		if (error != 0) {
			if (reterr) {
				nd->nd_repstat = NFSERR_ACCES;
				free(fs, M_STATFS);
				return (0);
			}
			NFSCLRSTATFS_ATTRBIT(retbitp);
		}
	}
#endif

	/*
	 * And the NFSv4 ACL...
	 */
	if (NFSISSET_ATTRBIT(retbitp, NFSATTRBIT_ACLSUPPORT) &&
	    (nfsrv_useacl == 0 || ((cred != NULL || p != NULL) &&
		supports_nfsv4acls == 0))) {
		NFSCLRBIT_ATTRBIT(retbitp, NFSATTRBIT_ACLSUPPORT);
	}
	if (NFSISSET_ATTRBIT(retbitp, NFSATTRBIT_ACL)) {
		if (nfsrv_useacl == 0 || ((cred != NULL || p != NULL) &&
		    supports_nfsv4acls == 0)) {
			NFSCLRBIT_ATTRBIT(retbitp, NFSATTRBIT_ACL);
		} else if (naclp != NULL) {
			if (NFSVOPLOCK(vp, LK_SHARED) == 0) {
				error = VOP_ACCESSX(vp, VREAD_ACL, cred, p);
				if (error == 0)
					error = VOP_GETACL(vp, ACL_TYPE_NFS4,
					    naclp, cred, p);
				NFSVOPUNLOCK(vp, 0);
			} else
				error = NFSERR_PERM;
			if (error != 0) {
				if (reterr) {
					nd->nd_repstat = NFSERR_ACCES;
					free(fs, M_STATFS);
					return (0);
				}
				NFSCLRBIT_ATTRBIT(retbitp, NFSATTRBIT_ACL);
			}
		}
	}

	/*
	 * Put out the attribute bitmap for the ones being filled in
	 * and get the field for the number of attributes returned.
	 */
	prefixnum = nfsrv_putattrbit(nd, retbitp);
	NFSM_BUILD(retnump, u_int32_t *, NFSX_UNSIGNED);
	prefixnum += NFSX_UNSIGNED;

	/*
	 * Now, loop around filling in the attributes for each bit set.
	 */
	for (bitpos = 0; bitpos < NFSATTRBIT_MAX; bitpos++) {
	    if (NFSISSET_ATTRBIT(retbitp, bitpos)) {
		switch (bitpos) {
		case NFSATTRBIT_SUPPORTEDATTRS:
			NFSSETSUPP_ATTRBIT(&attrbits);
			if (nfsrv_useacl == 0 || ((cred != NULL || p != NULL)
			    && supports_nfsv4acls == 0)) {
			    NFSCLRBIT_ATTRBIT(&attrbits,NFSATTRBIT_ACLSUPPORT);
			    NFSCLRBIT_ATTRBIT(&attrbits,NFSATTRBIT_ACL);
			}
			retnum += nfsrv_putattrbit(nd, &attrbits);
			break;
		case NFSATTRBIT_TYPE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = vtonfsv34_type(vap->va_type);
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_FHEXPIRETYPE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV4FHTYPE_PERSISTENT);
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CHANGE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			txdr_hyper(vap->va_filerev, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SIZE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			txdr_hyper(vap->va_size, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_LINKSUPPORT:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			if (fsinf.fs_properties & NFSV3FSINFO_LINK)
				*tl = newnfs_true;
			else
				*tl = newnfs_false;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_SYMLINKSUPPORT:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			if (fsinf.fs_properties & NFSV3FSINFO_SYMLINK)
				*tl = newnfs_true;
			else
				*tl = newnfs_false;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_NAMEDATTR:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_false;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_FSID:
			NFSM_BUILD(tl, u_int32_t *, NFSX_V4FSID);
			*tl++ = 0;
			*tl++ = txdr_unsigned(mp->mnt_stat.f_fsid.val[0]);
			*tl++ = 0;
			*tl = txdr_unsigned(mp->mnt_stat.f_fsid.val[1]);
			retnum += NFSX_V4FSID;
			break;
		case NFSATTRBIT_UNIQUEHANDLES:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_true;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_LEASETIME:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(nfsrv_lease);
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_RDATTRERROR:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(rderror);
			retnum += NFSX_UNSIGNED;
			break;
		/*
		 * Recommended Attributes. (Only the supported ones.)
		 */
		case NFSATTRBIT_ACL:
			retnum += nfsrv_buildacl(nd, aclp, vnode_vtype(vp), p);
			break;
		case NFSATTRBIT_ACLSUPPORT:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV4ACE_SUPTYPES);
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CANSETTIME:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			if (fsinf.fs_properties & NFSV3FSINFO_CANSETTIME)
				*tl = newnfs_true;
			else
				*tl = newnfs_false;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CASEINSENSITIVE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_false;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CASEPRESERVING:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_true;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_CHOWNRESTRICTED:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_true;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_FILEHANDLE:
			retnum += nfsm_fhtom(nd, (u_int8_t *)fhp, 0, 0);
			break;
		case NFSATTRBIT_FILEID:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			uquad = vap->va_fileid;
			txdr_hyper(uquad, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_FILESAVAIL:
			/*
			 * Check quota and use min(quota, f_ffree).
			 */
			freenum = fs->f_ffree;
#ifdef QUOTA
			/*
			 * ufs_quotactl() insists that the uid argument
			 * equal p_ruid for non-root quota access, so
			 * we'll just make sure that's the case.
			 */
			savuid = p->p_cred->p_ruid;
			p->p_cred->p_ruid = cred->cr_uid;
			if (!VFS_QUOTACTL(mp, QCMD(Q_GETQUOTA,USRQUOTA),
			    cred->cr_uid, (caddr_t)&dqb))
			    freenum = min(dqb.dqb_isoftlimit-dqb.dqb_curinodes,
				freenum);
			p->p_cred->p_ruid = savuid;
#endif	/* QUOTA */
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			*tl++ = 0;
			*tl = txdr_unsigned(freenum);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_FILESFREE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			*tl++ = 0;
			*tl = txdr_unsigned(fs->f_ffree);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_FILESTOTAL:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			*tl++ = 0;
			*tl = txdr_unsigned(fs->f_files);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_FSLOCATIONS:
			NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
			*tl++ = 0;
			*tl = 0;
			retnum += 2 * NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_HOMOGENEOUS:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			if (fsinf.fs_properties & NFSV3FSINFO_HOMOGENEOUS)
				*tl = newnfs_true;
			else
				*tl = newnfs_false;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_MAXFILESIZE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			uquad = NFSRV_MAXFILESIZE;
			txdr_hyper(uquad, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_MAXLINK:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFS_LINK_MAX);
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_MAXNAME:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFS_MAXNAMLEN);
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_MAXREAD:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			*tl++ = 0;
			*tl = txdr_unsigned(fsinf.fs_rtmax);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_MAXWRITE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			*tl++ = 0;
			*tl = txdr_unsigned(fsinf.fs_wtmax);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_MODE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = vtonfsv34_mode(vap->va_mode);
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_NOTRUNC:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = newnfs_true;
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_NUMLINKS:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(vap->va_nlink);
			retnum += NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_OWNER:
			cp = namestr;
			nfsv4_uidtostr(vap->va_uid, &cp, &siz);
			retnum += nfsm_strtom(nd, cp, siz);
			if (cp != namestr)
				free(cp, M_NFSSTRING);
			break;
		case NFSATTRBIT_OWNERGROUP:
			cp = namestr;
			nfsv4_gidtostr(vap->va_gid, &cp, &siz);
			retnum += nfsm_strtom(nd, cp, siz);
			if (cp != namestr)
				free(cp, M_NFSSTRING);
			break;
		case NFSATTRBIT_QUOTAHARD:
			if (priv_check_cred(cred, PRIV_VFS_EXCEEDQUOTA))
				freenum = fs->f_bfree;
			else
				freenum = fs->f_bavail;
#ifdef QUOTA
			/*
			 * ufs_quotactl() insists that the uid argument
			 * equal p_ruid for non-root quota access, so
			 * we'll just make sure that's the case.
			 */
			savuid = p->p_cred->p_ruid;
			p->p_cred->p_ruid = cred->cr_uid;
			if (!VFS_QUOTACTL(mp, QCMD(Q_GETQUOTA,USRQUOTA),
			    cred->cr_uid, (caddr_t)&dqb))
			    freenum = min(dqb.dqb_bhardlimit, freenum);
			p->p_cred->p_ruid = savuid;
#endif	/* QUOTA */
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			uquad = (u_int64_t)freenum;
			NFSQUOTABLKTOBYTE(uquad, fs->f_bsize);
			txdr_hyper(uquad, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_QUOTASOFT:
			if (priv_check_cred(cred, PRIV_VFS_EXCEEDQUOTA))
				freenum = fs->f_bfree;
			else
				freenum = fs->f_bavail;
#ifdef QUOTA
			/*
			 * ufs_quotactl() insists that the uid argument
			 * equal p_ruid for non-root quota access, so
			 * we'll just make sure that's the case.
			 */
			savuid = p->p_cred->p_ruid;
			p->p_cred->p_ruid = cred->cr_uid;
			if (!VFS_QUOTACTL(mp, QCMD(Q_GETQUOTA,USRQUOTA),
			    cred->cr_uid, (caddr_t)&dqb))
			    freenum = min(dqb.dqb_bsoftlimit, freenum);
			p->p_cred->p_ruid = savuid;
#endif	/* QUOTA */
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			uquad = (u_int64_t)freenum;
			NFSQUOTABLKTOBYTE(uquad, fs->f_bsize);
			txdr_hyper(uquad, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_QUOTAUSED:
			freenum = 0;
#ifdef QUOTA
			/*
			 * ufs_quotactl() insists that the uid argument
			 * equal p_ruid for non-root quota access, so
			 * we'll just make sure that's the case.
			 */
			savuid = p->p_cred->p_ruid;
			p->p_cred->p_ruid = cred->cr_uid;
			if (!VFS_QUOTACTL(mp, QCMD(Q_GETQUOTA,USRQUOTA),
			    cred->cr_uid, (caddr_t)&dqb))
			    freenum = dqb.dqb_curblocks;
			p->p_cred->p_ruid = savuid;
#endif	/* QUOTA */
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			uquad = (u_int64_t)freenum;
			NFSQUOTABLKTOBYTE(uquad, fs->f_bsize);
			txdr_hyper(uquad, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_RAWDEV:
			NFSM_BUILD(tl, u_int32_t *, NFSX_V4SPECDATA);
			*tl++ = txdr_unsigned(NFSMAJOR(vap->va_rdev));
			*tl = txdr_unsigned(NFSMINOR(vap->va_rdev));
			retnum += NFSX_V4SPECDATA;
			break;
		case NFSATTRBIT_SPACEAVAIL:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			if (priv_check_cred(cred, PRIV_VFS_BLOCKRESERVE)) {
				if (pnfssf != NULL)
					uquad = (u_int64_t)pnfssf->f_bfree;
				else
					uquad = (u_int64_t)fs->f_bfree;
			} else {
				if (pnfssf != NULL)
					uquad = (u_int64_t)pnfssf->f_bavail;
				else
					uquad = (u_int64_t)fs->f_bavail;
			}
			if (pnfssf != NULL)
				uquad *= pnfssf->f_bsize;
			else
				uquad *= fs->f_bsize;
			txdr_hyper(uquad, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SPACEFREE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			if (pnfssf != NULL) {
				uquad = (u_int64_t)pnfssf->f_bfree;
				uquad *= pnfssf->f_bsize;
			} else {
				uquad = (u_int64_t)fs->f_bfree;
				uquad *= fs->f_bsize;
			}
			txdr_hyper(uquad, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SPACETOTAL:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			if (pnfssf != NULL) {
				uquad = (u_int64_t)pnfssf->f_blocks;
				uquad *= pnfssf->f_bsize;
			} else {
				uquad = (u_int64_t)fs->f_blocks;
				uquad *= fs->f_bsize;
			}
			txdr_hyper(uquad, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SPACEUSED:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			txdr_hyper(vap->va_bytes, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_TIMEACCESS:
			NFSM_BUILD(tl, u_int32_t *, NFSX_V4TIME);
			txdr_nfsv4time(&vap->va_atime, tl);
			retnum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEACCESSSET:
			if ((vap->va_vaflags & VA_UTIMES_NULL) == 0) {
				NFSM_BUILD(tl, u_int32_t *, NFSX_V4SETTIME);
				*tl++ = txdr_unsigned(NFSV4SATTRTIME_TOCLIENT);
				txdr_nfsv4time(&vap->va_atime, tl);
				retnum += NFSX_V4SETTIME;
			} else {
				NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
				*tl = txdr_unsigned(NFSV4SATTRTIME_TOSERVER);
				retnum += NFSX_UNSIGNED;
			}
			break;
		case NFSATTRBIT_TIMEDELTA:
			NFSM_BUILD(tl, u_int32_t *, NFSX_V4TIME);
			temptime.tv_sec = 0;
			temptime.tv_nsec = 1000000000 / hz;
			txdr_nfsv4time(&temptime, tl);
			retnum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEMETADATA:
			NFSM_BUILD(tl, u_int32_t *, NFSX_V4TIME);
			txdr_nfsv4time(&vap->va_ctime, tl);
			retnum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEMODIFY:
			NFSM_BUILD(tl, u_int32_t *, NFSX_V4TIME);
			txdr_nfsv4time(&vap->va_mtime, tl);
			retnum += NFSX_V4TIME;
			break;
		case NFSATTRBIT_TIMEMODIFYSET:
			if ((vap->va_vaflags & VA_UTIMES_NULL) == 0) {
				NFSM_BUILD(tl, u_int32_t *, NFSX_V4SETTIME);
				*tl++ = txdr_unsigned(NFSV4SATTRTIME_TOCLIENT);
				txdr_nfsv4time(&vap->va_mtime, tl);
				retnum += NFSX_V4SETTIME;
			} else {
				NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
				*tl = txdr_unsigned(NFSV4SATTRTIME_TOSERVER);
				retnum += NFSX_UNSIGNED;
			}
			break;
		case NFSATTRBIT_MOUNTEDONFILEID:
			NFSM_BUILD(tl, u_int32_t *, NFSX_HYPER);
			if (at_root != 0)
				uquad = mounted_on_fileno;
			else
				uquad = vap->va_fileid;
			txdr_hyper(uquad, tl);
			retnum += NFSX_HYPER;
			break;
		case NFSATTRBIT_SUPPATTREXCLCREAT:
			NFSSETSUPP_ATTRBIT(&attrbits);
			NFSCLRNOTSETABLE_ATTRBIT(&attrbits);
			NFSCLRBIT_ATTRBIT(&attrbits, NFSATTRBIT_TIMEACCESSSET);
			retnum += nfsrv_putattrbit(nd, &attrbits);
			break;
		case NFSATTRBIT_FSLAYOUTTYPE:
		case NFSATTRBIT_LAYOUTTYPE:
			if (nfsrv_devidcnt == 0)
				siz = 1;
			else
				siz = 2;
			if (siz == 2) {
				NFSM_BUILD(tl, u_int32_t *, 2 * NFSX_UNSIGNED);
				*tl++ = txdr_unsigned(1);	/* One entry. */
				if (nfsrv_doflexfile != 0 ||
				    nfsrv_maxpnfsmirror > 1)
					*tl = txdr_unsigned(NFSLAYOUT_FLEXFILE);
				else
					*tl = txdr_unsigned(
					    NFSLAYOUT_NFSV4_1_FILES);
			} else {
				NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
				*tl = 0;
			}
			retnum += siz * NFSX_UNSIGNED;
			break;
		case NFSATTRBIT_LAYOUTALIGNMENT:
		case NFSATTRBIT_LAYOUTBLKSIZE:
			NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFS_SRVMAXIO);
			retnum += NFSX_UNSIGNED;
			break;
		default:
			printf("EEK! Bad V4 attribute bitpos=%d\n", bitpos);
		}
	    }
	}
	if (naclp != NULL)
		acl_free(naclp);
	free(fs, M_STATFS);
	*retnump = txdr_unsigned(retnum);
	return (retnum + prefixnum);
}

/*
 * Put the attribute bits onto an mbuf list.
 * Return the number of bytes of output generated.
 */
APPLESTATIC int
nfsrv_putattrbit(struct nfsrv_descript *nd, nfsattrbit_t *attrbitp)
{
	u_int32_t *tl;
	int cnt, i, bytesize;

	for (cnt = NFSATTRBIT_MAXWORDS; cnt > 0; cnt--)
		if (attrbitp->bits[cnt - 1])
			break;
	bytesize = (cnt + 1) * NFSX_UNSIGNED;
	NFSM_BUILD(tl, u_int32_t *, bytesize);
	*tl++ = txdr_unsigned(cnt);
	for (i = 0; i < cnt; i++)
		*tl++ = txdr_unsigned(attrbitp->bits[i]);
	return (bytesize);
}

/*
 * Convert a uid to a string.
 * If the lookup fails, just output the digits.
 * uid - the user id
 * cpp - points to a buffer of size NFSV4_SMALLSTR
 *       (malloc a larger one, as required)
 * retlenp - pointer to length to be returned
 */
APPLESTATIC void
nfsv4_uidtostr(uid_t uid, u_char **cpp, int *retlenp)
{
	int i;
	struct nfsusrgrp *usrp;
	u_char *cp = *cpp;
	uid_t tmp;
	int cnt, hasampersand, len = NFSV4_SMALLSTR, ret;
	struct nfsrv_lughash *hp;

	cnt = 0;
tryagain:
	if (nfsrv_dnsnamelen > 0 && !nfs_enable_uidtostring) {
		/*
		 * Always map nfsrv_defaultuid to "nobody".
		 */
		if (uid == nfsrv_defaultuid) {
			i = nfsrv_dnsnamelen + 7;
			if (i > len) {
				if (len > NFSV4_SMALLSTR)
					free(cp, M_NFSSTRING);
				cp = malloc(i, M_NFSSTRING, M_WAITOK);
				*cpp = cp;
				len = i;
				goto tryagain;
			}
			*retlenp = i;
			NFSBCOPY("nobody@", cp, 7);
			cp += 7;
			NFSBCOPY(nfsrv_dnsname, cp, nfsrv_dnsnamelen);
			return;
		}
		hasampersand = 0;
		hp = NFSUSERHASH(uid);
		mtx_lock(&hp->mtx);
		TAILQ_FOREACH(usrp, &hp->lughead, lug_numhash) {
			if (usrp->lug_uid == uid) {
				if (usrp->lug_expiry < NFSD_MONOSEC)
					break;
				/*
				 * If the name doesn't already have an '@'
				 * in it, append @domainname to it.
				 */
				for (i = 0; i < usrp->lug_namelen; i++) {
					if (usrp->lug_name[i] == '@') {
						hasampersand = 1;
						break;
					}
				}
				if (hasampersand)
					i = usrp->lug_namelen;
				else
					i = usrp->lug_namelen +
					    nfsrv_dnsnamelen + 1;
				if (i > len) {
					mtx_unlock(&hp->mtx);
					if (len > NFSV4_SMALLSTR)
						free(cp, M_NFSSTRING);
					cp = malloc(i, M_NFSSTRING, M_WAITOK);
					*cpp = cp;
					len = i;
					goto tryagain;
				}
				*retlenp = i;
				NFSBCOPY(usrp->lug_name, cp, usrp->lug_namelen);
				if (!hasampersand) {
					cp += usrp->lug_namelen;
					*cp++ = '@';
					NFSBCOPY(nfsrv_dnsname, cp, nfsrv_dnsnamelen);
				}
				TAILQ_REMOVE(&hp->lughead, usrp, lug_numhash);
				TAILQ_INSERT_TAIL(&hp->lughead, usrp,
				    lug_numhash);
				mtx_unlock(&hp->mtx);
				return;
			}
		}
		mtx_unlock(&hp->mtx);
		cnt++;
		ret = nfsrv_getuser(RPCNFSUSERD_GETUID, uid, (gid_t)0, NULL);
		if (ret == 0 && cnt < 2)
			goto tryagain;
	}

	/*
	 * No match, just return a string of digits.
	 */
	tmp = uid;
	i = 0;
	while (tmp || i == 0) {
		tmp /= 10;
		i++;
	}
	len = (i > len) ? len : i;
	*retlenp = len;
	cp += (len - 1);
	tmp = uid;
	for (i = 0; i < len; i++) {
		*cp-- = '0' + (tmp % 10);
		tmp /= 10;
	}
	return;
}

/*
 * Get a credential for the uid with the server's group list.
 * If none is found, just return the credential passed in after
 * logging a warning message.
 */
struct ucred *
nfsrv_getgrpscred(struct ucred *oldcred)
{
	struct nfsusrgrp *usrp;
	struct ucred *newcred;
	int cnt, ret;
	uid_t uid;
	struct nfsrv_lughash *hp;

	cnt = 0;
	uid = oldcred->cr_uid;
tryagain:
	if (nfsrv_dnsnamelen > 0) {
		hp = NFSUSERHASH(uid);
		mtx_lock(&hp->mtx);
		TAILQ_FOREACH(usrp, &hp->lughead, lug_numhash) {
			if (usrp->lug_uid == uid) {
				if (usrp->lug_expiry < NFSD_MONOSEC)
					break;
				if (usrp->lug_cred != NULL) {
					newcred = crhold(usrp->lug_cred);
					crfree(oldcred);
				} else
					newcred = oldcred;
				TAILQ_REMOVE(&hp->lughead, usrp, lug_numhash);
				TAILQ_INSERT_TAIL(&hp->lughead, usrp,
				    lug_numhash);
				mtx_unlock(&hp->mtx);
				return (newcred);
			}
		}
		mtx_unlock(&hp->mtx);
		cnt++;
		ret = nfsrv_getuser(RPCNFSUSERD_GETUID, uid, (gid_t)0, NULL);
		if (ret == 0 && cnt < 2)
			goto tryagain;
	}
	return (oldcred);
}

/*
 * Convert a string to a uid.
 * If no conversion is possible return NFSERR_BADOWNER, otherwise
 * return 0.
 * If this is called from a client side mount using AUTH_SYS and the
 * string is made up entirely of digits, just convert the string to
 * a number.
 */
APPLESTATIC int
nfsv4_strtouid(struct nfsrv_descript *nd, u_char *str, int len, uid_t *uidp)
{
	int i;
	char *cp, *endstr, *str0;
	struct nfsusrgrp *usrp;
	int cnt, ret;
	int error = 0;
	uid_t tuid;
	struct nfsrv_lughash *hp, *hp2;

	if (len == 0) {
		error = NFSERR_BADOWNER;
		goto out;
	}
	/* If a string of digits and an AUTH_SYS mount, just convert it. */
	str0 = str;
	tuid = (uid_t)strtoul(str0, &endstr, 10);
	if ((endstr - str0) == len) {
		/* A numeric string. */
		if ((nd->nd_flag & ND_KERBV) == 0 &&
		    ((nd->nd_flag & ND_NFSCL) != 0 ||
		      nfsd_enable_stringtouid != 0))
			*uidp = tuid;
		else
			error = NFSERR_BADOWNER;
		goto out;
	}
	/*
	 * Look for an '@'.
	 */
	cp = strchr(str0, '@');
	if (cp != NULL)
		i = (int)(cp++ - str0);
	else
		i = len;

	cnt = 0;
tryagain:
	if (nfsrv_dnsnamelen > 0) {
		/*
		 * If an '@' is found and the domain name matches, search for
		 * the name with dns stripped off.
		 * Mixed case alpahbetics will match for the domain name, but
		 * all upper case will not.
		 */
		if (cnt == 0 && i < len && i > 0 &&
		    (len - 1 - i) == nfsrv_dnsnamelen &&
		    !nfsrv_cmpmixedcase(cp, nfsrv_dnsname, nfsrv_dnsnamelen)) {
			len -= (nfsrv_dnsnamelen + 1);
			*(cp - 1) = '\0';
		}
	
		/*
		 * Check for the special case of "nobody".
		 */
		if (len == 6 && !NFSBCMP(str, "nobody", 6)) {
			*uidp = nfsrv_defaultuid;
			error = 0;
			goto out;
		}
	
		hp = NFSUSERNAMEHASH(str, len);
		mtx_lock(&hp->mtx);
		TAILQ_FOREACH(usrp, &hp->lughead, lug_namehash) {
			if (usrp->lug_namelen == len &&
			    !NFSBCMP(usrp->lug_name, str, len)) {
				if (usrp->lug_expiry < NFSD_MONOSEC)
					break;
				hp2 = NFSUSERHASH(usrp->lug_uid);
				mtx_lock(&hp2->mtx);
				TAILQ_REMOVE(&hp2->lughead, usrp, lug_numhash);
				TAILQ_INSERT_TAIL(&hp2->lughead, usrp,
				    lug_numhash);
				*uidp = usrp->lug_uid;
				mtx_unlock(&hp2->mtx);
				mtx_unlock(&hp->mtx);
				error = 0;
				goto out;
			}
		}
		mtx_unlock(&hp->mtx);
		cnt++;
		ret = nfsrv_getuser(RPCNFSUSERD_GETUSER, (uid_t)0, (gid_t)0,
		    str);
		if (ret == 0 && cnt < 2)
			goto tryagain;
	}
	error = NFSERR_BADOWNER;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Convert a gid to a string.
 * gid - the group id
 * cpp - points to a buffer of size NFSV4_SMALLSTR
 *       (malloc a larger one, as required)
 * retlenp - pointer to length to be returned
 */
APPLESTATIC void
nfsv4_gidtostr(gid_t gid, u_char **cpp, int *retlenp)
{
	int i;
	struct nfsusrgrp *usrp;
	u_char *cp = *cpp;
	gid_t tmp;
	int cnt, hasampersand, len = NFSV4_SMALLSTR, ret;
	struct nfsrv_lughash *hp;

	cnt = 0;
tryagain:
	if (nfsrv_dnsnamelen > 0 && !nfs_enable_uidtostring) {
		/*
		 * Always map nfsrv_defaultgid to "nogroup".
		 */
		if (gid == nfsrv_defaultgid) {
			i = nfsrv_dnsnamelen + 8;
			if (i > len) {
				if (len > NFSV4_SMALLSTR)
					free(cp, M_NFSSTRING);
				cp = malloc(i, M_NFSSTRING, M_WAITOK);
				*cpp = cp;
				len = i;
				goto tryagain;
			}
			*retlenp = i;
			NFSBCOPY("nogroup@", cp, 8);
			cp += 8;
			NFSBCOPY(nfsrv_dnsname, cp, nfsrv_dnsnamelen);
			return;
		}
		hasampersand = 0;
		hp = NFSGROUPHASH(gid);
		mtx_lock(&hp->mtx);
		TAILQ_FOREACH(usrp, &hp->lughead, lug_numhash) {
			if (usrp->lug_gid == gid) {
				if (usrp->lug_expiry < NFSD_MONOSEC)
					break;
				/*
				 * If the name doesn't already have an '@'
				 * in it, append @domainname to it.
				 */
				for (i = 0; i < usrp->lug_namelen; i++) {
					if (usrp->lug_name[i] == '@') {
						hasampersand = 1;
						break;
					}
				}
				if (hasampersand)
					i = usrp->lug_namelen;
				else
					i = usrp->lug_namelen +
					    nfsrv_dnsnamelen + 1;
				if (i > len) {
					mtx_unlock(&hp->mtx);
					if (len > NFSV4_SMALLSTR)
						free(cp, M_NFSSTRING);
					cp = malloc(i, M_NFSSTRING, M_WAITOK);
					*cpp = cp;
					len = i;
					goto tryagain;
				}
				*retlenp = i;
				NFSBCOPY(usrp->lug_name, cp, usrp->lug_namelen);
				if (!hasampersand) {
					cp += usrp->lug_namelen;
					*cp++ = '@';
					NFSBCOPY(nfsrv_dnsname, cp, nfsrv_dnsnamelen);
				}
				TAILQ_REMOVE(&hp->lughead, usrp, lug_numhash);
				TAILQ_INSERT_TAIL(&hp->lughead, usrp,
				    lug_numhash);
				mtx_unlock(&hp->mtx);
				return;
			}
		}
		mtx_unlock(&hp->mtx);
		cnt++;
		ret = nfsrv_getuser(RPCNFSUSERD_GETGID, (uid_t)0, gid, NULL);
		if (ret == 0 && cnt < 2)
			goto tryagain;
	}

	/*
	 * No match, just return a string of digits.
	 */
	tmp = gid;
	i = 0;
	while (tmp || i == 0) {
		tmp /= 10;
		i++;
	}
	len = (i > len) ? len : i;
	*retlenp = len;
	cp += (len - 1);
	tmp = gid;
	for (i = 0; i < len; i++) {
		*cp-- = '0' + (tmp % 10);
		tmp /= 10;
	}
	return;
}

/*
 * Convert a string to a gid.
 * If no conversion is possible return NFSERR_BADOWNER, otherwise
 * return 0.
 * If this is called from a client side mount using AUTH_SYS and the
 * string is made up entirely of digits, just convert the string to
 * a number.
 */
APPLESTATIC int
nfsv4_strtogid(struct nfsrv_descript *nd, u_char *str, int len, gid_t *gidp)
{
	int i;
	char *cp, *endstr, *str0;
	struct nfsusrgrp *usrp;
	int cnt, ret;
	int error = 0;
	gid_t tgid;
	struct nfsrv_lughash *hp, *hp2;

	if (len == 0) {
		error =  NFSERR_BADOWNER;
		goto out;
	}
	/* If a string of digits and an AUTH_SYS mount, just convert it. */
	str0 = str;
	tgid = (gid_t)strtoul(str0, &endstr, 10);
	if ((endstr - str0) == len) {
		/* A numeric string. */
		if ((nd->nd_flag & ND_KERBV) == 0 &&
		    ((nd->nd_flag & ND_NFSCL) != 0 ||
		      nfsd_enable_stringtouid != 0))
			*gidp = tgid;
		else
			error = NFSERR_BADOWNER;
		goto out;
	}
	/*
	 * Look for an '@'.
	 */
	cp = strchr(str0, '@');
	if (cp != NULL)
		i = (int)(cp++ - str0);
	else
		i = len;

	cnt = 0;
tryagain:
	if (nfsrv_dnsnamelen > 0) {
		/*
		 * If an '@' is found and the dns name matches, search for the
		 * name with the dns stripped off.
		 */
		if (cnt == 0 && i < len && i > 0 &&
		    (len - 1 - i) == nfsrv_dnsnamelen &&
		    !nfsrv_cmpmixedcase(cp, nfsrv_dnsname, nfsrv_dnsnamelen)) {
			len -= (nfsrv_dnsnamelen + 1);
			*(cp - 1) = '\0';
		}
	
		/*
		 * Check for the special case of "nogroup".
		 */
		if (len == 7 && !NFSBCMP(str, "nogroup", 7)) {
			*gidp = nfsrv_defaultgid;
			error = 0;
			goto out;
		}
	
		hp = NFSGROUPNAMEHASH(str, len);
		mtx_lock(&hp->mtx);
		TAILQ_FOREACH(usrp, &hp->lughead, lug_namehash) {
			if (usrp->lug_namelen == len &&
			    !NFSBCMP(usrp->lug_name, str, len)) {
				if (usrp->lug_expiry < NFSD_MONOSEC)
					break;
				hp2 = NFSGROUPHASH(usrp->lug_gid);
				mtx_lock(&hp2->mtx);
				TAILQ_REMOVE(&hp2->lughead, usrp, lug_numhash);
				TAILQ_INSERT_TAIL(&hp2->lughead, usrp,
				    lug_numhash);
				*gidp = usrp->lug_gid;
				mtx_unlock(&hp2->mtx);
				mtx_unlock(&hp->mtx);
				error = 0;
				goto out;
			}
		}
		mtx_unlock(&hp->mtx);
		cnt++;
		ret = nfsrv_getuser(RPCNFSUSERD_GETGROUP, (uid_t)0, (gid_t)0,
		    str);
		if (ret == 0 && cnt < 2)
			goto tryagain;
	}
	error = NFSERR_BADOWNER;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Cmp len chars, allowing mixed case in the first argument to match lower
 * case in the second, but not if the first argument is all upper case.
 * Return 0 for a match, 1 otherwise.
 */
static int
nfsrv_cmpmixedcase(u_char *cp, u_char *cp2, int len)
{
	int i;
	u_char tmp;
	int fndlower = 0;

	for (i = 0; i < len; i++) {
		if (*cp >= 'A' && *cp <= 'Z') {
			tmp = *cp++ + ('a' - 'A');
		} else {
			tmp = *cp++;
			if (tmp >= 'a' && tmp <= 'z')
				fndlower = 1;
		}
		if (tmp != *cp2++)
			return (1);
	}
	if (fndlower)
		return (0);
	else
		return (1);
}

/*
 * Set the port for the nfsuserd.
 */
APPLESTATIC int
nfsrv_nfsuserdport(struct sockaddr *sad, u_short port, NFSPROC_T *p)
{
	struct nfssockreq *rp;
	struct sockaddr_in *ad;
	int error;

	NFSLOCKNAMEID();
	if (nfsrv_nfsuserd) {
		NFSUNLOCKNAMEID();
		error = EPERM;
		free(sad, M_SONAME);
		goto out;
	}
	nfsrv_nfsuserd = 1;
	NFSUNLOCKNAMEID();
	/*
	 * Set up the socket record and connect.
	 */
	rp = &nfsrv_nfsuserdsock;
	rp->nr_client = NULL;
	rp->nr_cred = NULL;
	rp->nr_lock = (NFSR_RESERVEDPORT | NFSR_LOCALHOST);
	if (sad != NULL) {
		/* Use the AF_LOCAL socket address passed in. */
		rp->nr_sotype = SOCK_STREAM;
		rp->nr_soproto = 0;
		rp->nr_nam = sad;
	} else {
		/* Use the port# for a UDP socket (old nfsuserd). */
		rp->nr_sotype = SOCK_DGRAM;
		rp->nr_soproto = IPPROTO_UDP;
		rp->nr_nam = malloc(sizeof(*rp->nr_nam), M_SONAME, M_WAITOK |
		    M_ZERO);
		NFSSOCKADDRSIZE(rp->nr_nam, sizeof (struct sockaddr_in));
		ad = NFSSOCKADDR(rp->nr_nam, struct sockaddr_in *);
		ad->sin_family = AF_INET;
		ad->sin_addr.s_addr = htonl((u_int32_t)0x7f000001);
		ad->sin_port = port;
	}
	rp->nr_prog = RPCPROG_NFSUSERD;
	rp->nr_vers = RPCNFSUSERD_VERS;
	error = newnfs_connect(NULL, rp, NFSPROCCRED(p), p, 0);
	if (error) {
		free(rp->nr_nam, M_SONAME);
		nfsrv_nfsuserd = 0;
	}
out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Delete the nfsuserd port.
 */
APPLESTATIC void
nfsrv_nfsuserddelport(void)
{

	NFSLOCKNAMEID();
	if (nfsrv_nfsuserd == 0) {
		NFSUNLOCKNAMEID();
		return;
	}
	nfsrv_nfsuserd = 0;
	NFSUNLOCKNAMEID();
	newnfs_disconnect(&nfsrv_nfsuserdsock);
	free(nfsrv_nfsuserdsock.nr_nam, M_SONAME);
}

/*
 * Do upcalls to the nfsuserd, for cache misses of the owner/ownergroup
 * name<-->id cache.
 * Returns 0 upon success, non-zero otherwise.
 */
static int
nfsrv_getuser(int procnum, uid_t uid, gid_t gid, char *name)
{
	u_int32_t *tl;
	struct nfsrv_descript *nd;
	int len;
	struct nfsrv_descript nfsd;
	struct ucred *cred;
	int error;

	NFSLOCKNAMEID();
	if (nfsrv_nfsuserd == 0) {
		NFSUNLOCKNAMEID();
		error = EPERM;
		goto out;
	}
	NFSUNLOCKNAMEID();
	nd = &nfsd;
	cred = newnfs_getcred();
	nd->nd_flag = ND_GSSINITREPLY;
	nfsrvd_rephead(nd);

	nd->nd_procnum = procnum;
	if (procnum == RPCNFSUSERD_GETUID || procnum == RPCNFSUSERD_GETGID) {
		NFSM_BUILD(tl, u_int32_t *, NFSX_UNSIGNED);
		if (procnum == RPCNFSUSERD_GETUID)
			*tl = txdr_unsigned(uid);
		else
			*tl = txdr_unsigned(gid);
	} else {
		len = strlen(name);
		(void) nfsm_strtom(nd, name, len);
	}
	error = newnfs_request(nd, NULL, NULL, &nfsrv_nfsuserdsock, NULL, NULL,
		cred, RPCPROG_NFSUSERD, RPCNFSUSERD_VERS, NULL, 0, NULL, NULL);
	NFSFREECRED(cred);
	if (!error) {
		mbuf_freem(nd->nd_mrep);
		error = nd->nd_repstat;
	}
out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * This function is called from the nfssvc(2) system call, to update the
 * kernel user/group name list(s) for the V4 owner and ownergroup attributes.
 */
APPLESTATIC int
nfssvc_idname(struct nfsd_idargs *nidp)
{
	struct nfsusrgrp *nusrp, *usrp, *newusrp;
	struct nfsrv_lughash *hp_name, *hp_idnum, *thp;
	int i, group_locked, groupname_locked, user_locked, username_locked;
	int error = 0;
	u_char *cp;
	gid_t *grps;
	struct ucred *cr;
	static int onethread = 0;
	static time_t lasttime = 0;

	if (nidp->nid_namelen <= 0 || nidp->nid_namelen > MAXHOSTNAMELEN) {
		error = EINVAL;
		goto out;
	}
	if (nidp->nid_flag & NFSID_INITIALIZE) {
		cp = malloc(nidp->nid_namelen + 1, M_NFSSTRING, M_WAITOK);
		error = copyin(CAST_USER_ADDR_T(nidp->nid_name), cp,
		    nidp->nid_namelen);
		if (error != 0) {
			free(cp, M_NFSSTRING);
			goto out;
		}
		if (atomic_cmpset_acq_int(&nfsrv_dnsnamelen, 0, 0) == 0) {
			/*
			 * Free up all the old stuff and reinitialize hash
			 * lists.  All mutexes for both lists must be locked,
			 * with the user/group name ones before the uid/gid
			 * ones, to avoid a LOR.
			 */
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_lock(&nfsusernamehash[i].mtx);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_lock(&nfsuserhash[i].mtx);
			for (i = 0; i < nfsrv_lughashsize; i++)
				TAILQ_FOREACH_SAFE(usrp,
				    &nfsuserhash[i].lughead, lug_numhash, nusrp)
					nfsrv_removeuser(usrp, 1);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_unlock(&nfsuserhash[i].mtx);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_unlock(&nfsusernamehash[i].mtx);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_lock(&nfsgroupnamehash[i].mtx);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_lock(&nfsgrouphash[i].mtx);
			for (i = 0; i < nfsrv_lughashsize; i++)
				TAILQ_FOREACH_SAFE(usrp,
				    &nfsgrouphash[i].lughead, lug_numhash,
				    nusrp)
					nfsrv_removeuser(usrp, 0);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_unlock(&nfsgrouphash[i].mtx);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_unlock(&nfsgroupnamehash[i].mtx);
			free(nfsrv_dnsname, M_NFSSTRING);
			nfsrv_dnsname = NULL;
		}
		if (nfsuserhash == NULL) {
			/* Allocate the hash tables. */
			nfsuserhash = malloc(sizeof(struct nfsrv_lughash) *
			    nfsrv_lughashsize, M_NFSUSERGROUP, M_WAITOK |
			    M_ZERO);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_init(&nfsuserhash[i].mtx, "nfsuidhash",
				    NULL, MTX_DEF | MTX_DUPOK);
			nfsusernamehash = malloc(sizeof(struct nfsrv_lughash) *
			    nfsrv_lughashsize, M_NFSUSERGROUP, M_WAITOK |
			    M_ZERO);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_init(&nfsusernamehash[i].mtx,
				    "nfsusrhash", NULL, MTX_DEF |
				    MTX_DUPOK);
			nfsgrouphash = malloc(sizeof(struct nfsrv_lughash) *
			    nfsrv_lughashsize, M_NFSUSERGROUP, M_WAITOK |
			    M_ZERO);
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_init(&nfsgrouphash[i].mtx, "nfsgidhash",
				    NULL, MTX_DEF | MTX_DUPOK);
			nfsgroupnamehash = malloc(sizeof(struct nfsrv_lughash) *
			    nfsrv_lughashsize, M_NFSUSERGROUP, M_WAITOK |
			    M_ZERO);
			for (i = 0; i < nfsrv_lughashsize; i++)
			    mtx_init(&nfsgroupnamehash[i].mtx,
			    "nfsgrphash", NULL, MTX_DEF | MTX_DUPOK);
		}
		/* (Re)initialize the list heads. */
		for (i = 0; i < nfsrv_lughashsize; i++)
			TAILQ_INIT(&nfsuserhash[i].lughead);
		for (i = 0; i < nfsrv_lughashsize; i++)
			TAILQ_INIT(&nfsusernamehash[i].lughead);
		for (i = 0; i < nfsrv_lughashsize; i++)
			TAILQ_INIT(&nfsgrouphash[i].lughead);
		for (i = 0; i < nfsrv_lughashsize; i++)
			TAILQ_INIT(&nfsgroupnamehash[i].lughead);

		/*
		 * Put name in "DNS" string.
		 */
		nfsrv_dnsname = cp;
		nfsrv_defaultuid = nidp->nid_uid;
		nfsrv_defaultgid = nidp->nid_gid;
		nfsrv_usercnt = 0;
		nfsrv_usermax = nidp->nid_usermax;
		atomic_store_rel_int(&nfsrv_dnsnamelen, nidp->nid_namelen);
		goto out;
	}

	/*
	 * malloc the new one now, so any potential sleep occurs before
	 * manipulation of the lists.
	 */
	newusrp = malloc(sizeof(struct nfsusrgrp) + nidp->nid_namelen,
	    M_NFSUSERGROUP, M_WAITOK | M_ZERO);
	error = copyin(CAST_USER_ADDR_T(nidp->nid_name), newusrp->lug_name,
	    nidp->nid_namelen);
	if (error == 0 && nidp->nid_ngroup > 0 &&
	    (nidp->nid_flag & NFSID_ADDUID) != 0) {
		grps = malloc(sizeof(gid_t) * nidp->nid_ngroup, M_TEMP,
		    M_WAITOK);
		error = copyin(CAST_USER_ADDR_T(nidp->nid_grps), grps,
		    sizeof(gid_t) * nidp->nid_ngroup);
		if (error == 0) {
			/*
			 * Create a credential just like svc_getcred(),
			 * but using the group list provided.
			 */
			cr = crget();
			cr->cr_uid = cr->cr_ruid = cr->cr_svuid = nidp->nid_uid;
			crsetgroups(cr, nidp->nid_ngroup, grps);
			cr->cr_rgid = cr->cr_svgid = cr->cr_groups[0];
			cr->cr_prison = &prison0;
			prison_hold(cr->cr_prison);
#ifdef MAC
			mac_cred_associate_nfsd(cr);
#endif
			newusrp->lug_cred = cr;
		}
		free(grps, M_TEMP);
	}
	if (error) {
		free(newusrp, M_NFSUSERGROUP);
		goto out;
	}
	newusrp->lug_namelen = nidp->nid_namelen;

	/*
	 * The lock order is username[0]->[nfsrv_lughashsize - 1] followed
	 * by uid[0]->[nfsrv_lughashsize - 1], with the same for group.
	 * The flags user_locked, username_locked, group_locked and
	 * groupname_locked are set to indicate all of those hash lists are
	 * locked. hp_name != NULL  and hp_idnum != NULL indicates that
	 * the respective one mutex is locked.
	 */
	user_locked = username_locked = group_locked = groupname_locked = 0;
	hp_name = hp_idnum = NULL;

	/*
	 * Delete old entries, as required.
	 */
	if (nidp->nid_flag & (NFSID_DELUID | NFSID_ADDUID)) {
		/* Must lock all username hash lists first, to avoid a LOR. */
		for (i = 0; i < nfsrv_lughashsize; i++)
			mtx_lock(&nfsusernamehash[i].mtx);
		username_locked = 1;
		hp_idnum = NFSUSERHASH(nidp->nid_uid);
		mtx_lock(&hp_idnum->mtx);
		TAILQ_FOREACH_SAFE(usrp, &hp_idnum->lughead, lug_numhash,
		    nusrp) {
			if (usrp->lug_uid == nidp->nid_uid)
				nfsrv_removeuser(usrp, 1);
		}
	} else if (nidp->nid_flag & (NFSID_DELUSERNAME | NFSID_ADDUSERNAME)) {
		hp_name = NFSUSERNAMEHASH(newusrp->lug_name,
		    newusrp->lug_namelen);
		mtx_lock(&hp_name->mtx);
		TAILQ_FOREACH_SAFE(usrp, &hp_name->lughead, lug_namehash,
		    nusrp) {
			if (usrp->lug_namelen == newusrp->lug_namelen &&
			    !NFSBCMP(usrp->lug_name, newusrp->lug_name,
			    usrp->lug_namelen)) {
				thp = NFSUSERHASH(usrp->lug_uid);
				mtx_lock(&thp->mtx);
				nfsrv_removeuser(usrp, 1);
				mtx_unlock(&thp->mtx);
			}
		}
		hp_idnum = NFSUSERHASH(nidp->nid_uid);
		mtx_lock(&hp_idnum->mtx);
	} else if (nidp->nid_flag & (NFSID_DELGID | NFSID_ADDGID)) {
		/* Must lock all groupname hash lists first, to avoid a LOR. */
		for (i = 0; i < nfsrv_lughashsize; i++)
			mtx_lock(&nfsgroupnamehash[i].mtx);
		groupname_locked = 1;
		hp_idnum = NFSGROUPHASH(nidp->nid_gid);
		mtx_lock(&hp_idnum->mtx);
		TAILQ_FOREACH_SAFE(usrp, &hp_idnum->lughead, lug_numhash,
		    nusrp) {
			if (usrp->lug_gid == nidp->nid_gid)
				nfsrv_removeuser(usrp, 0);
		}
	} else if (nidp->nid_flag & (NFSID_DELGROUPNAME | NFSID_ADDGROUPNAME)) {
		hp_name = NFSGROUPNAMEHASH(newusrp->lug_name,
		    newusrp->lug_namelen);
		mtx_lock(&hp_name->mtx);
		TAILQ_FOREACH_SAFE(usrp, &hp_name->lughead, lug_namehash,
		    nusrp) {
			if (usrp->lug_namelen == newusrp->lug_namelen &&
			    !NFSBCMP(usrp->lug_name, newusrp->lug_name,
			    usrp->lug_namelen)) {
				thp = NFSGROUPHASH(usrp->lug_gid);
				mtx_lock(&thp->mtx);
				nfsrv_removeuser(usrp, 0);
				mtx_unlock(&thp->mtx);
			}
		}
		hp_idnum = NFSGROUPHASH(nidp->nid_gid);
		mtx_lock(&hp_idnum->mtx);
	}

	/*
	 * Now, we can add the new one.
	 */
	if (nidp->nid_usertimeout)
		newusrp->lug_expiry = NFSD_MONOSEC + nidp->nid_usertimeout;
	else
		newusrp->lug_expiry = NFSD_MONOSEC + 5;
	if (nidp->nid_flag & (NFSID_ADDUID | NFSID_ADDUSERNAME)) {
		newusrp->lug_uid = nidp->nid_uid;
		thp = NFSUSERHASH(newusrp->lug_uid);
		mtx_assert(&thp->mtx, MA_OWNED);
		TAILQ_INSERT_TAIL(&thp->lughead, newusrp, lug_numhash);
		thp = NFSUSERNAMEHASH(newusrp->lug_name, newusrp->lug_namelen);
		mtx_assert(&thp->mtx, MA_OWNED);
		TAILQ_INSERT_TAIL(&thp->lughead, newusrp, lug_namehash);
		atomic_add_int(&nfsrv_usercnt, 1);
	} else if (nidp->nid_flag & (NFSID_ADDGID | NFSID_ADDGROUPNAME)) {
		newusrp->lug_gid = nidp->nid_gid;
		thp = NFSGROUPHASH(newusrp->lug_gid);
		mtx_assert(&thp->mtx, MA_OWNED);
		TAILQ_INSERT_TAIL(&thp->lughead, newusrp, lug_numhash);
		thp = NFSGROUPNAMEHASH(newusrp->lug_name, newusrp->lug_namelen);
		mtx_assert(&thp->mtx, MA_OWNED);
		TAILQ_INSERT_TAIL(&thp->lughead, newusrp, lug_namehash);
		atomic_add_int(&nfsrv_usercnt, 1);
	} else {
		if (newusrp->lug_cred != NULL)
			crfree(newusrp->lug_cred);
		free(newusrp, M_NFSUSERGROUP);
	}

	/*
	 * Once per second, allow one thread to trim the cache.
	 */
	if (lasttime < NFSD_MONOSEC &&
	    atomic_cmpset_acq_int(&onethread, 0, 1) != 0) {
		/*
		 * First, unlock the single mutexes, so that all entries
		 * can be locked and any LOR is avoided.
		 */
		if (hp_name != NULL) {
			mtx_unlock(&hp_name->mtx);
			hp_name = NULL;
		}
		if (hp_idnum != NULL) {
			mtx_unlock(&hp_idnum->mtx);
			hp_idnum = NULL;
		}

		if ((nidp->nid_flag & (NFSID_DELUID | NFSID_ADDUID |
		    NFSID_DELUSERNAME | NFSID_ADDUSERNAME)) != 0) {
			if (username_locked == 0) {
				for (i = 0; i < nfsrv_lughashsize; i++)
					mtx_lock(&nfsusernamehash[i].mtx);
				username_locked = 1;
			}
			KASSERT(user_locked == 0,
			    ("nfssvc_idname: user_locked"));
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_lock(&nfsuserhash[i].mtx);
			user_locked = 1;
			for (i = 0; i < nfsrv_lughashsize; i++) {
				TAILQ_FOREACH_SAFE(usrp,
				    &nfsuserhash[i].lughead, lug_numhash,
				    nusrp)
					if (usrp->lug_expiry < NFSD_MONOSEC)
						nfsrv_removeuser(usrp, 1);
			}
			for (i = 0; i < nfsrv_lughashsize; i++) {
				/*
				 * Trim the cache using an approximate LRU
				 * algorithm.  This code deletes the least
				 * recently used entry on each hash list.
				 */
				if (nfsrv_usercnt <= nfsrv_usermax)
					break;
				usrp = TAILQ_FIRST(&nfsuserhash[i].lughead);
				if (usrp != NULL)
					nfsrv_removeuser(usrp, 1);
			}
		} else {
			if (groupname_locked == 0) {
				for (i = 0; i < nfsrv_lughashsize; i++)
					mtx_lock(&nfsgroupnamehash[i].mtx);
				groupname_locked = 1;
			}
			KASSERT(group_locked == 0,
			    ("nfssvc_idname: group_locked"));
			for (i = 0; i < nfsrv_lughashsize; i++)
				mtx_lock(&nfsgrouphash[i].mtx);
			group_locked = 1;
			for (i = 0; i < nfsrv_lughashsize; i++) {
				TAILQ_FOREACH_SAFE(usrp,
				    &nfsgrouphash[i].lughead, lug_numhash,
				    nusrp)
					if (usrp->lug_expiry < NFSD_MONOSEC)
						nfsrv_removeuser(usrp, 0);
			}
			for (i = 0; i < nfsrv_lughashsize; i++) {
				/*
				 * Trim the cache using an approximate LRU
				 * algorithm.  This code deletes the least
				 * recently user entry on each hash list.
				 */
				if (nfsrv_usercnt <= nfsrv_usermax)
					break;
				usrp = TAILQ_FIRST(&nfsgrouphash[i].lughead);
				if (usrp != NULL)
					nfsrv_removeuser(usrp, 0);
			}
		}
		lasttime = NFSD_MONOSEC;
		atomic_store_rel_int(&onethread, 0);
	}

	/* Now, unlock all locked mutexes. */
	if (hp_idnum != NULL)
		mtx_unlock(&hp_idnum->mtx);
	if (hp_name != NULL)
		mtx_unlock(&hp_name->mtx);
	if (user_locked != 0)
		for (i = 0; i < nfsrv_lughashsize; i++)
			mtx_unlock(&nfsuserhash[i].mtx);
	if (username_locked != 0)
		for (i = 0; i < nfsrv_lughashsize; i++)
			mtx_unlock(&nfsusernamehash[i].mtx);
	if (group_locked != 0)
		for (i = 0; i < nfsrv_lughashsize; i++)
			mtx_unlock(&nfsgrouphash[i].mtx);
	if (groupname_locked != 0)
		for (i = 0; i < nfsrv_lughashsize; i++)
			mtx_unlock(&nfsgroupnamehash[i].mtx);
out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Remove a user/group name element.
 */
static void
nfsrv_removeuser(struct nfsusrgrp *usrp, int isuser)
{
	struct nfsrv_lughash *hp;

	if (isuser != 0) {
		hp = NFSUSERHASH(usrp->lug_uid);
		mtx_assert(&hp->mtx, MA_OWNED);
		TAILQ_REMOVE(&hp->lughead, usrp, lug_numhash);
		hp = NFSUSERNAMEHASH(usrp->lug_name, usrp->lug_namelen);
		mtx_assert(&hp->mtx, MA_OWNED);
		TAILQ_REMOVE(&hp->lughead, usrp, lug_namehash);
	} else {
		hp = NFSGROUPHASH(usrp->lug_gid);
		mtx_assert(&hp->mtx, MA_OWNED);
		TAILQ_REMOVE(&hp->lughead, usrp, lug_numhash);
		hp = NFSGROUPNAMEHASH(usrp->lug_name, usrp->lug_namelen);
		mtx_assert(&hp->mtx, MA_OWNED);
		TAILQ_REMOVE(&hp->lughead, usrp, lug_namehash);
	}
	atomic_add_int(&nfsrv_usercnt, -1);
	if (usrp->lug_cred != NULL)
		crfree(usrp->lug_cred);
	free(usrp, M_NFSUSERGROUP);
}

/*
 * Free up all the allocations related to the name<-->id cache.
 * This function should only be called when the nfsuserd daemon isn't
 * running, since it doesn't do any locking.
 * This function is meant to be used when the nfscommon module is unloaded.
 */
APPLESTATIC void
nfsrv_cleanusergroup(void)
{
	struct nfsrv_lughash *hp, *hp2;
	struct nfsusrgrp *nusrp, *usrp;
	int i;

	if (nfsuserhash == NULL)
		return;

	for (i = 0; i < nfsrv_lughashsize; i++) {
		hp = &nfsuserhash[i];
		TAILQ_FOREACH_SAFE(usrp, &hp->lughead, lug_numhash, nusrp) {
			TAILQ_REMOVE(&hp->lughead, usrp, lug_numhash);
			hp2 = NFSUSERNAMEHASH(usrp->lug_name,
			    usrp->lug_namelen);
			TAILQ_REMOVE(&hp2->lughead, usrp, lug_namehash);
			if (usrp->lug_cred != NULL)
				crfree(usrp->lug_cred);
			free(usrp, M_NFSUSERGROUP);
		}
		hp = &nfsgrouphash[i];
		TAILQ_FOREACH_SAFE(usrp, &hp->lughead, lug_numhash, nusrp) {
			TAILQ_REMOVE(&hp->lughead, usrp, lug_numhash);
			hp2 = NFSGROUPNAMEHASH(usrp->lug_name,
			    usrp->lug_namelen);
			TAILQ_REMOVE(&hp2->lughead, usrp, lug_namehash);
			if (usrp->lug_cred != NULL)
				crfree(usrp->lug_cred);
			free(usrp, M_NFSUSERGROUP);
		}
		mtx_destroy(&nfsuserhash[i].mtx);
		mtx_destroy(&nfsusernamehash[i].mtx);
		mtx_destroy(&nfsgroupnamehash[i].mtx);
		mtx_destroy(&nfsgrouphash[i].mtx);
	}
	free(nfsuserhash, M_NFSUSERGROUP);
	free(nfsusernamehash, M_NFSUSERGROUP);
	free(nfsgrouphash, M_NFSUSERGROUP);
	free(nfsgroupnamehash, M_NFSUSERGROUP);
	free(nfsrv_dnsname, M_NFSSTRING);
}

/*
 * This function scans a byte string and checks for UTF-8 compliance.
 * It returns 0 if it conforms and NFSERR_INVAL if not.
 */
APPLESTATIC int
nfsrv_checkutf8(u_int8_t *cp, int len)
{
	u_int32_t val = 0x0;
	int cnt = 0, gotd = 0, shift = 0;
	u_int8_t byte;
	static int utf8_shift[5] = { 7, 11, 16, 21, 26 };
	int error = 0;

	/*
	 * Here are what the variables are used for:
	 * val - the calculated value of a multibyte char, used to check
	 *       that it was coded with the correct range
	 * cnt - the number of 10xxxxxx bytes to follow
	 * gotd - set for a char of Dxxx, so D800<->DFFF can be checked for
	 * shift - lower order bits of range (ie. "val >> shift" should
	 *       not be 0, in other words, dividing by the lower bound
	 *       of the range should get a non-zero value)
	 * byte - used to calculate cnt
	 */
	while (len > 0) {
		if (cnt > 0) {
			/* This handles the 10xxxxxx bytes */
			if ((*cp & 0xc0) != 0x80 ||
			    (gotd && (*cp & 0x20))) {
				error = NFSERR_INVAL;
				goto out;
			}
			gotd = 0;
			val <<= 6;
			val |= (*cp & 0x3f);
			cnt--;
			if (cnt == 0 && (val >> shift) == 0x0) {
				error = NFSERR_INVAL;
				goto out;
			}
		} else if (*cp & 0x80) {
			/* first byte of multi byte char */
			byte = *cp;
			while ((byte & 0x40) && cnt < 6) {
				cnt++;
				byte <<= 1;
			}
			if (cnt == 0 || cnt == 6) {
				error = NFSERR_INVAL;
				goto out;
			}
			val = (*cp & (0x3f >> cnt));
			shift = utf8_shift[cnt - 1];
			if (cnt == 2 && val == 0xd)
				/* Check for the 0xd800-0xdfff case */
				gotd = 1;
		}
		cp++;
		len--;
	}
	if (cnt > 0)
		error = NFSERR_INVAL;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * Parse the xdr for an NFSv4 FsLocations attribute. Return two malloc'd
 * strings, one with the root path in it and the other with the list of
 * locations. The list is in the same format as is found in nfr_refs.
 * It is a "," separated list of entries, where each of them is of the
 * form <server>:<rootpath>. For example
 * "nfsv4-test:/sub2,nfsv4-test2:/user/mnt,nfsv4-test2:/user/mnt2"
 * The nilp argument is set to 1 for the special case of a null fs_root
 * and an empty server list.
 * It returns NFSERR_BADXDR, if the xdr can't be parsed and returns the
 * number of xdr bytes parsed in sump.
 */
static int
nfsrv_getrefstr(struct nfsrv_descript *nd, u_char **fsrootp, u_char **srvp,
    int *sump, int *nilp)
{
	u_int32_t *tl;
	u_char *cp = NULL, *cp2 = NULL, *cp3, *str;
	int i, j, len, stringlen, cnt, slen, siz, xdrsum, error = 0, nsrv;
	struct list {
		SLIST_ENTRY(list) next;
		int len;
		u_char host[1];
	} *lsp, *nlsp;
	SLIST_HEAD(, list) head;

	*fsrootp = NULL;
	*srvp = NULL;
	*nilp = 0;

	/*
	 * Get the fs_root path and check for the special case of null path
	 * and 0 length server list.
	 */
	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	len = fxdr_unsigned(int, *tl);
	if (len < 0 || len > 10240) {
		error = NFSERR_BADXDR;
		goto nfsmout;
	}
	if (len == 0) {
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		if (*tl != 0) {
			error = NFSERR_BADXDR;
			goto nfsmout;
		}
		*nilp = 1;
		*sump = 2 * NFSX_UNSIGNED;
		error = 0;
		goto nfsmout;
	}
	cp = malloc(len + 1, M_NFSSTRING, M_WAITOK);
	error = nfsrv_mtostr(nd, cp, len);
	if (!error) {
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		cnt = fxdr_unsigned(int, *tl);
		if (cnt <= 0)
			error = NFSERR_BADXDR;
	}
	if (error)
		goto nfsmout;

	/*
	 * Now, loop through the location list and make up the srvlist.
	 */
	xdrsum = (2 * NFSX_UNSIGNED) + NFSM_RNDUP(len);
	cp2 = cp3 = malloc(1024, M_NFSSTRING, M_WAITOK);
	slen = 1024;
	siz = 0;
	for (i = 0; i < cnt; i++) {
		SLIST_INIT(&head);
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		nsrv = fxdr_unsigned(int, *tl);
		if (nsrv <= 0) {
			error = NFSERR_BADXDR;
			goto nfsmout;
		}

		/*
		 * Handle the first server by putting it in the srvstr.
		 */
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		len = fxdr_unsigned(int, *tl);
		if (len <= 0 || len > 1024) {
			error = NFSERR_BADXDR;
			goto nfsmout;
		}
		nfsrv_refstrbigenough(siz + len + 3, &cp2, &cp3, &slen);
		if (cp3 != cp2) {
			*cp3++ = ',';
			siz++;
		}
		error = nfsrv_mtostr(nd, cp3, len);
		if (error)
			goto nfsmout;
		cp3 += len;
		*cp3++ = ':';
		siz += (len + 1);
		xdrsum += (2 * NFSX_UNSIGNED) + NFSM_RNDUP(len);
		for (j = 1; j < nsrv; j++) {
			/*
			 * Yuck, put them in an slist and process them later.
			 */
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			len = fxdr_unsigned(int, *tl);
			if (len <= 0 || len > 1024) {
				error = NFSERR_BADXDR;
				goto nfsmout;
			}
			lsp = (struct list *)malloc(sizeof (struct list)
			    + len, M_TEMP, M_WAITOK);
			error = nfsrv_mtostr(nd, lsp->host, len);
			if (error)
				goto nfsmout;
			xdrsum += NFSX_UNSIGNED + NFSM_RNDUP(len);
			lsp->len = len;
			SLIST_INSERT_HEAD(&head, lsp, next);
		}

		/*
		 * Finally, we can get the path.
		 */
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		len = fxdr_unsigned(int, *tl);
		if (len <= 0 || len > 1024) {
			error = NFSERR_BADXDR;
			goto nfsmout;
		}
		nfsrv_refstrbigenough(siz + len + 1, &cp2, &cp3, &slen);
		error = nfsrv_mtostr(nd, cp3, len);
		if (error)
			goto nfsmout;
		xdrsum += NFSX_UNSIGNED + NFSM_RNDUP(len);
		str = cp3;
		stringlen = len;
		cp3 += len;
		siz += len;
		SLIST_FOREACH_SAFE(lsp, &head, next, nlsp) {
			nfsrv_refstrbigenough(siz + lsp->len + stringlen + 3,
			    &cp2, &cp3, &slen);
			*cp3++ = ',';
			NFSBCOPY(lsp->host, cp3, lsp->len);
			cp3 += lsp->len;
			*cp3++ = ':';
			NFSBCOPY(str, cp3, stringlen);
			cp3 += stringlen;
			*cp3 = '\0';
			siz += (lsp->len + stringlen + 2);
			free(lsp, M_TEMP);
		}
	}
	*fsrootp = cp;
	*srvp = cp2;
	*sump = xdrsum;
	NFSEXITCODE2(0, nd);
	return (0);
nfsmout:
	if (cp != NULL)
		free(cp, M_NFSSTRING);
	if (cp2 != NULL)
		free(cp2, M_NFSSTRING);
	NFSEXITCODE2(error, nd);
	return (error);
}

/*
 * Make the malloc'd space large enough. This is a pain, but the xdr
 * doesn't set an upper bound on the side, so...
 */
static void
nfsrv_refstrbigenough(int siz, u_char **cpp, u_char **cpp2, int *slenp)
{
	u_char *cp;
	int i;

	if (siz <= *slenp)
		return;
	cp = malloc(siz + 1024, M_NFSSTRING, M_WAITOK);
	NFSBCOPY(*cpp, cp, *slenp);
	free(*cpp, M_NFSSTRING);
	i = *cpp2 - *cpp;
	*cpp = cp;
	*cpp2 = cp + i;
	*slenp = siz + 1024;
}

/*
 * Initialize the reply header data structures.
 */
APPLESTATIC void
nfsrvd_rephead(struct nfsrv_descript *nd)
{
	mbuf_t mreq;

	/*
	 * If this is a big reply, use a cluster.
	 */
	if ((nd->nd_flag & ND_GSSINITREPLY) == 0 &&
	    nfs_bigreply[nd->nd_procnum]) {
		NFSMCLGET(mreq, M_WAITOK);
		nd->nd_mreq = mreq;
		nd->nd_mb = mreq;
	} else {
		NFSMGET(mreq);
		nd->nd_mreq = mreq;
		nd->nd_mb = mreq;
	}
	nd->nd_bpos = NFSMTOD(mreq, caddr_t);
	mbuf_setlen(mreq, 0);

	if ((nd->nd_flag & ND_GSSINITREPLY) == 0)
		NFSM_BUILD(nd->nd_errp, int *, NFSX_UNSIGNED);
}

/*
 * Lock a socket against others.
 * Currently used to serialize connect/disconnect attempts.
 */
int
newnfs_sndlock(int *flagp)
{
	struct timespec ts;

	NFSLOCKSOCK();
	while (*flagp & NFSR_SNDLOCK) {
		*flagp |= NFSR_WANTSND;
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
		(void) nfsmsleep((caddr_t)flagp, NFSSOCKMUTEXPTR,
		    PZERO - 1, "nfsndlck", &ts);
	}
	*flagp |= NFSR_SNDLOCK;
	NFSUNLOCKSOCK();
	return (0);
}

/*
 * Unlock the stream socket for others.
 */
void
newnfs_sndunlock(int *flagp)
{

	NFSLOCKSOCK();
	if ((*flagp & NFSR_SNDLOCK) == 0)
		panic("nfs sndunlock");
	*flagp &= ~NFSR_SNDLOCK;
	if (*flagp & NFSR_WANTSND) {
		*flagp &= ~NFSR_WANTSND;
		wakeup((caddr_t)flagp);
	}
	NFSUNLOCKSOCK();
}

APPLESTATIC int
nfsv4_getipaddr(struct nfsrv_descript *nd, struct sockaddr_in *sin,
    struct sockaddr_in6 *sin6, sa_family_t *saf, int *isudp)
{
	struct in_addr saddr;
	uint32_t portnum, *tl;
	int i, j, k;
	sa_family_t af = AF_UNSPEC;
	char addr[64], protocol[5], *cp;
	int cantparse = 0, error = 0;
	uint16_t portv;

	NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
	i = fxdr_unsigned(int, *tl);
	if (i >= 3 && i <= 4) {
		error = nfsrv_mtostr(nd, protocol, i);
		if (error)
			goto nfsmout;
		if (strcmp(protocol, "tcp") == 0) {
			af = AF_INET;
			*isudp = 0;
		} else if (strcmp(protocol, "udp") == 0) {
			af = AF_INET;
			*isudp = 1;
		} else if (strcmp(protocol, "tcp6") == 0) {
			af = AF_INET6;
			*isudp = 0;
		} else if (strcmp(protocol, "udp6") == 0) {
			af = AF_INET6;
			*isudp = 1;
		} else
			cantparse = 1;
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
	} else if (cantparse == 0 && i >= 11 && i < 64) {
		/*
		 * The shortest address is 11chars and the longest is < 64.
		 */
		error = nfsrv_mtostr(nd, addr, i);
		if (error)
			goto nfsmout;

		/* Find the port# at the end and extract that. */
		i = strlen(addr);
		k = 0;
		cp = &addr[i - 1];
		/* Count back two '.'s from end to get port# field. */
		for (j = 0; j < i; j++) {
			if (*cp == '.') {
				k++;
				if (k == 2)
					break;
			}
			cp--;
		}
		if (k == 2) {
			/*
			 * The NFSv4 port# is appended as .N.N, where N is
			 * a decimal # in the range 0-255, just like an inet4
			 * address. Cheat and use inet_aton(), which will
			 * return a Class A address and then shift the high
			 * order 8bits over to convert it to the port#.
			 */
			*cp++ = '\0';
			if (inet_aton(cp, &saddr) == 1) {
				portnum = ntohl(saddr.s_addr);
				portv = (uint16_t)((portnum >> 16) |
				    (portnum & 0xff));
			} else
				cantparse = 1;
		} else
			cantparse = 1;
		if (cantparse == 0) {
			if (af == AF_INET) {
				if (inet_pton(af, addr, &sin->sin_addr) == 1) {
					sin->sin_len = sizeof(*sin);
					sin->sin_family = AF_INET;
					sin->sin_port = htons(portv);
					*saf = af;
					return (0);
				}
			} else {
				if (inet_pton(af, addr, &sin6->sin6_addr)
				    == 1) {
					sin6->sin6_len = sizeof(*sin6);
					sin6->sin6_family = AF_INET6;
					sin6->sin6_port = htons(portv);
					*saf = af;
					return (0);
				}
			}
		}
	} else {
		if (i > 0) {
			error = nfsm_advance(nd, NFSM_RNDUP(i), -1);
			if (error)
				goto nfsmout;
		}
	}
	error = EPERM;
nfsmout:
	return (error);
}

/*
 * Handle an NFSv4.1 Sequence request for the session.
 * If reply != NULL, use it to return the cached reply, as required.
 * The client gets a cached reply via this call for callbacks, however the
 * server gets a cached reply via the nfsv4_seqsess_cachereply() call.
 */
int
nfsv4_seqsession(uint32_t seqid, uint32_t slotid, uint32_t highslot,
    struct nfsslot *slots, struct mbuf **reply, uint16_t maxslot)
{
	int error;

	error = 0;
	if (reply != NULL)
		*reply = NULL;
	if (slotid > maxslot)
		return (NFSERR_BADSLOT);
	if (seqid == slots[slotid].nfssl_seq) {
		/* A retry. */
		if (slots[slotid].nfssl_inprog != 0)
			error = NFSERR_DELAY;
		else if (slots[slotid].nfssl_reply != NULL) {
			if (reply != NULL) {
				*reply = slots[slotid].nfssl_reply;
				slots[slotid].nfssl_reply = NULL;
			}
			slots[slotid].nfssl_inprog = 1;
			error = NFSERR_REPLYFROMCACHE;
		} else
			/* No reply cached, so just do it. */
			slots[slotid].nfssl_inprog = 1;
	} else if ((slots[slotid].nfssl_seq + 1) == seqid) {
		if (slots[slotid].nfssl_reply != NULL)
			m_freem(slots[slotid].nfssl_reply);
		slots[slotid].nfssl_reply = NULL;
		slots[slotid].nfssl_inprog = 1;
		slots[slotid].nfssl_seq++;
	} else
		error = NFSERR_SEQMISORDERED;
	return (error);
}

/*
 * Cache this reply for the slot.
 * Use the "rep" argument to return the cached reply if repstat is set to
 * NFSERR_REPLYFROMCACHE. The client never sets repstat to this value.
 */
void
nfsv4_seqsess_cacherep(uint32_t slotid, struct nfsslot *slots, int repstat,
   struct mbuf **rep)
{

	if (repstat == NFSERR_REPLYFROMCACHE) {
		*rep = slots[slotid].nfssl_reply;
		slots[slotid].nfssl_reply = NULL;
	} else {
		if (slots[slotid].nfssl_reply != NULL)
			m_freem(slots[slotid].nfssl_reply);
		slots[slotid].nfssl_reply = *rep;
	}
	slots[slotid].nfssl_inprog = 0;
}

/*
 * Generate the xdr for an NFSv4.1 Sequence Operation.
 */
APPLESTATIC void
nfsv4_setsequence(struct nfsmount *nmp, struct nfsrv_descript *nd,
    struct nfsclsession *sep, int dont_replycache)
{
	uint32_t *tl, slotseq = 0;
	int error, maxslot, slotpos;
	uint8_t sessionid[NFSX_V4SESSIONID];

	error = nfsv4_sequencelookup(nmp, sep, &slotpos, &maxslot, &slotseq,
	    sessionid);

	/* Build the Sequence arguments. */
	NFSM_BUILD(tl, uint32_t *, NFSX_V4SESSIONID + 4 * NFSX_UNSIGNED);
	nd->nd_sequence = tl;
	bcopy(sessionid, tl, NFSX_V4SESSIONID);
	tl += NFSX_V4SESSIONID / NFSX_UNSIGNED;
	nd->nd_slotseq = tl;
	if (error == 0) {
		nd->nd_flag |= ND_HASSLOTID;
		nd->nd_slotid = slotpos;
		*tl++ = txdr_unsigned(slotseq);
		*tl++ = txdr_unsigned(slotpos);
		*tl++ = txdr_unsigned(maxslot);
		if (dont_replycache == 0)
			*tl = newnfs_true;
		else
			*tl = newnfs_false;
	} else {
		/*
		 * There are two errors and the rest of the session can
		 * just be zeros.
		 * NFSERR_BADSESSION: This bad session should just generate
		 *    the same error again when the RPC is retried.
		 * ESTALE: A forced dismount is in progress and will cause the
		 *    RPC to fail later.
		 */
		*tl++ = 0;
		*tl++ = 0;
		*tl++ = 0;
		*tl = 0;
	}
	nd->nd_flag |= ND_HASSEQUENCE;
}

int
nfsv4_sequencelookup(struct nfsmount *nmp, struct nfsclsession *sep,
    int *slotposp, int *maxslotp, uint32_t *slotseqp, uint8_t *sessionid)
{
	int i, maxslot, slotpos;
	uint64_t bitval;

	/* Find an unused slot. */
	slotpos = -1;
	maxslot = -1;
	mtx_lock(&sep->nfsess_mtx);
	do {
		if (nmp != NULL && sep->nfsess_defunct != 0) {
			/* Just return the bad session. */
			bcopy(sep->nfsess_sessionid, sessionid,
			    NFSX_V4SESSIONID);
			mtx_unlock(&sep->nfsess_mtx);
			return (NFSERR_BADSESSION);
		}
		bitval = 1;
		for (i = 0; i < sep->nfsess_foreslots; i++) {
			if ((bitval & sep->nfsess_slots) == 0) {
				slotpos = i;
				sep->nfsess_slots |= bitval;
				sep->nfsess_slotseq[i]++;
				*slotseqp = sep->nfsess_slotseq[i];
				break;
			}
			bitval <<= 1;
		}
		if (slotpos == -1) {
			/*
			 * If a forced dismount is in progress, just return.
			 * This RPC attempt will fail when it calls
			 * newnfs_request().
			 */
			if (nmp != NULL && NFSCL_FORCEDISM(nmp->nm_mountp)) {
				mtx_unlock(&sep->nfsess_mtx);
				return (ESTALE);
			}
			/* Wake up once/sec, to check for a forced dismount. */
			(void)mtx_sleep(&sep->nfsess_slots, &sep->nfsess_mtx,
			    PZERO, "nfsclseq", hz);
		}
	} while (slotpos == -1);
	/* Now, find the highest slot in use. (nfsc_slots is 64bits) */
	bitval = 1;
	for (i = 0; i < 64; i++) {
		if ((bitval & sep->nfsess_slots) != 0)
			maxslot = i;
		bitval <<= 1;
	}
	bcopy(sep->nfsess_sessionid, sessionid, NFSX_V4SESSIONID);
	mtx_unlock(&sep->nfsess_mtx);
	*slotposp = slotpos;
	*maxslotp = maxslot;
	return (0);
}

/*
 * Free a session slot.
 */
APPLESTATIC void
nfsv4_freeslot(struct nfsclsession *sep, int slot)
{
	uint64_t bitval;

	bitval = 1;
	if (slot > 0)
		bitval <<= slot;
	mtx_lock(&sep->nfsess_mtx);
	if ((bitval & sep->nfsess_slots) == 0)
		printf("freeing free slot!!\n");
	sep->nfsess_slots &= ~bitval;
	wakeup(&sep->nfsess_slots);
	mtx_unlock(&sep->nfsess_mtx);
}

/*
 * Search for a matching pnfsd DS, based on the nmp arg.
 * Return one if found, NULL otherwise.
 */
struct nfsdevice *
nfsv4_findmirror(struct nfsmount *nmp)
{
	struct nfsdevice *ds;

	mtx_assert(NFSDDSMUTEXPTR, MA_OWNED);
	/*
	 * Search the DS server list for a match with nmp.
	 */
	if (nfsrv_devidcnt == 0)
		return (NULL);
	TAILQ_FOREACH(ds, &nfsrv_devidhead, nfsdev_list) {
		if (ds->nfsdev_nmp == nmp) {
			NFSCL_DEBUG(4, "nfsv4_findmirror: fnd main ds\n");
			break;
		}
	}
	return (ds);
}

