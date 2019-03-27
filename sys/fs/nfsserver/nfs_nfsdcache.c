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
 * Here is the basic algorithm:
 * First, some design criteria I used:
 * - I think a false hit is more serious than a false miss
 * - A false hit for an RPC that has Op(s) that order via seqid# must be
 *   avoided at all cost
 * - A valid hit will probably happen a long time after the original reply
 *   and the TCP socket that the original request was received on will no
 *   longer be active
 *   (The long time delay implies to me that LRU is not appropriate.)
 * - The mechanism will satisfy the requirements of ordering Ops with seqid#s
 *   in them as well as minimizing the risk of redoing retried non-idempotent
 *   Ops.
 * Because it is biased towards avoiding false hits, multiple entries with
 * the same xid are to be expected, especially for the case of the entry
 * in the cache being related to a seqid# sequenced Op.
 * 
 * The basic algorithm I'm about to code up:
 * - Null RPCs bypass the cache and are just done
 * For TCP
 * 	- key on <xid, NFS version> (as noted above, there can be several
 * 				     entries with the same key)
 * 	When a request arrives:
 * 		For all that match key
 * 		- if RPC# != OR request_size !=
 * 			- not a match with this one
 * 		- if NFSv4 and received on same TCP socket OR
 *			received on a TCP connection created before the
 *			entry was cached
 * 			- not a match with this one
 * 			(V2,3 clients might retry on same TCP socket)
 * 		- calculate checksum on first N bytes of NFS XDR
 * 		- if checksum !=
 * 			- not a match for this one
 * 		If any of the remaining ones that match has a
 * 			seqid_refcnt > 0
 * 			- not a match (go do RPC, using new cache entry)
 * 		If one match left
 * 			- a hit (reply from cache)
 * 		else
 * 			- miss (go do RPC, using new cache entry)
 * 
 * 	During processing of NFSv4 request:
 * 		- set a flag when a non-idempotent Op is processed
 * 		- when an Op that uses a seqid# (Open,...) is processed
 * 			- if same seqid# as referenced entry in cache
 * 				- free new cache entry
 * 				- reply from referenced cache entry
 * 			  else if next seqid# in order
 * 				- free referenced cache entry
 * 				- increment seqid_refcnt on new cache entry
 * 				- set pointer from Openowner/Lockowner to
 * 					new cache entry (aka reference it)
 * 			  else if first seqid# in sequence
 * 				- increment seqid_refcnt on new cache entry
 * 				- set pointer from Openowner/Lockowner to
 * 					new cache entry (aka reference it)
 * 
 * 	At end of RPC processing:
 * 		- if seqid_refcnt > 0 OR flagged non-idempotent on new
 * 			cache entry
 * 			- save reply in cache entry
 * 			- calculate checksum on first N bytes of NFS XDR
 * 				request
 * 			- note op and length of XDR request (in bytes)
 * 			- timestamp it
 * 		  else
 * 			- free new cache entry
 * 		- Send reply (noting info for socket activity check, below)
 * 
 * 	For cache entries saved above:
 * 		- if saved since seqid_refcnt was > 0
 * 			- free when seqid_refcnt decrements to 0
 * 			  (when next one in sequence is processed above, or
 * 			   when Openowner/Lockowner is discarded)
 * 		  else { non-idempotent Op(s) }
 * 			- free when
 * 				- some further activity observed on same
 * 					socket
 * 				  (I'm not yet sure how I'm going to do
 * 				   this. Maybe look at the TCP connection
 * 				   to see if the send_tcp_sequence# is well
 * 				   past sent reply OR K additional RPCs
 * 				   replied on same socket OR?)
 * 			  OR
 * 				- when very old (hours, days, weeks?)
 * 
 * For UDP (v2, 3 only), pretty much the old way:
 * - key on <xid, NFS version, RPC#, Client host ip#>
 *   (at most one entry for each key)
 * 
 * When a Request arrives:
 * - if a match with entry via key
 * 	- if RPC marked In_progress
 * 		- discard request (don't send reply)
 * 	  else
 * 		- reply from cache
 * 		- timestamp cache entry
 *   else
 * 	- add entry to cache, marked In_progress
 * 	- do RPC
 * 	- when RPC done
 * 		- if RPC# non-idempotent
 * 			- mark entry Done (not In_progress)
 * 			- save reply
 * 			- timestamp cache entry
 * 		  else
 * 			- free cache entry
 * 		- send reply
 * 
 * Later, entries with saved replies are free'd a short time (few minutes)
 * after reply sent (timestamp).
 * Reference: Chet Juszczak, "Improving the Performance and Correctness
 *		of an NFS Server", in Proc. Winter 1989 USENIX Conference,
 *		pages 53-63. San Diego, February 1989.
 *	 for the UDP case.
 * nfsrc_floodlevel is set to the allowable upper limit for saved replies
 *	for TCP. For V3, a reply won't be saved when the flood level is
 *	hit. For V4, the non-idempotent Op will return NFSERR_RESOURCE in
 *	that case. This level should be set high enough that this almost
 *	never happens.
 */
#ifndef APPLEKEXT
#include <fs/nfs/nfsport.h>

extern struct nfsstatsv1 nfsstatsv1;
extern struct mtx nfsrc_udpmtx;
extern struct nfsrchash_bucket nfsrchash_table[NFSRVCACHE_HASHSIZE];
extern struct nfsrchash_bucket nfsrcahash_table[NFSRVCACHE_HASHSIZE];
int nfsrc_floodlevel = NFSRVCACHE_FLOODLEVEL, nfsrc_tcpsavedreplies = 0;
#endif	/* !APPLEKEXT */

SYSCTL_DECL(_vfs_nfsd);

static u_int	nfsrc_tcphighwater = 0;
static int
sysctl_tcphighwater(SYSCTL_HANDLER_ARGS)
{
	int error, newhighwater;

	newhighwater = nfsrc_tcphighwater;
	error = sysctl_handle_int(oidp, &newhighwater, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (newhighwater < 0)
		return (EINVAL);
	if (newhighwater >= nfsrc_floodlevel)
		nfsrc_floodlevel = newhighwater + newhighwater / 5;
	nfsrc_tcphighwater = newhighwater;
	return (0);
}
SYSCTL_PROC(_vfs_nfsd, OID_AUTO, tcphighwater, CTLTYPE_UINT | CTLFLAG_RW, 0,
    sizeof(nfsrc_tcphighwater), sysctl_tcphighwater, "IU",
    "High water mark for TCP cache entries");

static u_int	nfsrc_udphighwater = NFSRVCACHE_UDPHIGHWATER;
SYSCTL_UINT(_vfs_nfsd, OID_AUTO, udphighwater, CTLFLAG_RW,
    &nfsrc_udphighwater, 0,
    "High water mark for UDP cache entries");
static u_int	nfsrc_tcptimeout = NFSRVCACHE_TCPTIMEOUT;
SYSCTL_UINT(_vfs_nfsd, OID_AUTO, tcpcachetimeo, CTLFLAG_RW,
    &nfsrc_tcptimeout, 0,
    "Timeout for TCP entries in the DRC");
static u_int nfsrc_tcpnonidempotent = 1;
SYSCTL_UINT(_vfs_nfsd, OID_AUTO, cachetcp, CTLFLAG_RW,
    &nfsrc_tcpnonidempotent, 0,
    "Enable the DRC for NFS over TCP");

static int nfsrc_udpcachesize = 0;
static TAILQ_HEAD(, nfsrvcache) nfsrvudplru;
static struct nfsrvhashhead nfsrvudphashtbl[NFSRVCACHE_HASHSIZE];

/*
 * and the reverse mapping from generic to Version 2 procedure numbers
 */
static int newnfsv2_procid[NFS_V3NPROCS] = {
	NFSV2PROC_NULL,
	NFSV2PROC_GETATTR,
	NFSV2PROC_SETATTR,
	NFSV2PROC_LOOKUP,
	NFSV2PROC_NOOP,
	NFSV2PROC_READLINK,
	NFSV2PROC_READ,
	NFSV2PROC_WRITE,
	NFSV2PROC_CREATE,
	NFSV2PROC_MKDIR,
	NFSV2PROC_SYMLINK,
	NFSV2PROC_CREATE,
	NFSV2PROC_REMOVE,
	NFSV2PROC_RMDIR,
	NFSV2PROC_RENAME,
	NFSV2PROC_LINK,
	NFSV2PROC_READDIR,
	NFSV2PROC_NOOP,
	NFSV2PROC_STATFS,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
};

#define	nfsrc_hash(xid)	(((xid) + ((xid) >> 24)) % NFSRVCACHE_HASHSIZE)
#define	NFSRCUDPHASH(xid) \
	(&nfsrvudphashtbl[nfsrc_hash(xid)])
#define	NFSRCHASH(xid) \
	(&nfsrchash_table[nfsrc_hash(xid)].tbl)
#define	NFSRCAHASH(xid) (&nfsrcahash_table[nfsrc_hash(xid)])
#define	TRUE	1
#define	FALSE	0
#define	NFSRVCACHE_CHECKLEN	100

/* True iff the rpc reply is an nfs status ONLY! */
static int nfsv2_repstat[NFS_V3NPROCS] = {
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	TRUE,
	TRUE,
	TRUE,
	TRUE,
	FALSE,
	TRUE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
	FALSE,
};

/*
 * Will NFS want to work over IPv6 someday?
 */
#define	NETFAMILY(rp) \
		(((rp)->rc_flag & RC_INETIPV6) ? AF_INET6 : AF_INET)

/* local functions */
static int nfsrc_getudp(struct nfsrv_descript *nd, struct nfsrvcache *newrp);
static int nfsrc_gettcp(struct nfsrv_descript *nd, struct nfsrvcache *newrp);
static void nfsrc_lock(struct nfsrvcache *rp);
static void nfsrc_unlock(struct nfsrvcache *rp);
static void nfsrc_wanted(struct nfsrvcache *rp);
static void nfsrc_freecache(struct nfsrvcache *rp);
static int nfsrc_getlenandcksum(mbuf_t m1, u_int16_t *cksum);
static void nfsrc_marksametcpconn(u_int64_t);

/*
 * Return the correct mutex for this cache entry.
 */
static __inline struct mtx *
nfsrc_cachemutex(struct nfsrvcache *rp)
{

	if ((rp->rc_flag & RC_UDP) != 0)
		return (&nfsrc_udpmtx);
	return (&nfsrchash_table[nfsrc_hash(rp->rc_xid)].mtx);
}

/*
 * Initialize the server request cache list
 */
APPLESTATIC void
nfsrvd_initcache(void)
{
	int i;
	static int inited = 0;

	if (inited)
		return;
	inited = 1;
	for (i = 0; i < NFSRVCACHE_HASHSIZE; i++) {
		LIST_INIT(&nfsrvudphashtbl[i]);
		LIST_INIT(&nfsrchash_table[i].tbl);
		LIST_INIT(&nfsrcahash_table[i].tbl);
	}
	TAILQ_INIT(&nfsrvudplru);
	nfsrc_tcpsavedreplies = 0;
	nfsrc_udpcachesize = 0;
	nfsstatsv1.srvcache_tcppeak = 0;
	nfsstatsv1.srvcache_size = 0;
}

/*
 * Get a cache entry for this request. Basically just malloc a new one
 * and then call nfsrc_getudp() or nfsrc_gettcp() to do the rest.
 */
APPLESTATIC int
nfsrvd_getcache(struct nfsrv_descript *nd)
{
	struct nfsrvcache *newrp;
	int ret;

	if (nd->nd_procnum == NFSPROC_NULL)
		panic("nfsd cache null");
	newrp = malloc(sizeof (struct nfsrvcache),
	    M_NFSRVCACHE, M_WAITOK);
	NFSBZERO((caddr_t)newrp, sizeof (struct nfsrvcache));
	if (nd->nd_flag & ND_NFSV4)
		newrp->rc_flag = RC_NFSV4;
	else if (nd->nd_flag & ND_NFSV3)
		newrp->rc_flag = RC_NFSV3;
	else
		newrp->rc_flag = RC_NFSV2;
	newrp->rc_xid = nd->nd_retxid;
	newrp->rc_proc = nd->nd_procnum;
	newrp->rc_sockref = nd->nd_sockref;
	newrp->rc_cachetime = nd->nd_tcpconntime;
	if (nd->nd_flag & ND_SAMETCPCONN)
		newrp->rc_flag |= RC_SAMETCPCONN;
	if (nd->nd_nam2 != NULL) {
		newrp->rc_flag |= RC_UDP;
		ret = nfsrc_getudp(nd, newrp);
	} else {
		ret = nfsrc_gettcp(nd, newrp);
	}
	NFSEXITCODE2(0, nd);
	return (ret);
}

/*
 * For UDP (v2, v3):
 * - key on <xid, NFS version, RPC#, Client host ip#>
 *   (at most one entry for each key)
 */
static int
nfsrc_getudp(struct nfsrv_descript *nd, struct nfsrvcache *newrp)
{
	struct nfsrvcache *rp;
	struct sockaddr_in *saddr;
	struct sockaddr_in6 *saddr6;
	struct nfsrvhashhead *hp;
	int ret = 0;
	struct mtx *mutex;

	mutex = nfsrc_cachemutex(newrp);
	hp = NFSRCUDPHASH(newrp->rc_xid);
loop:
	mtx_lock(mutex);
	LIST_FOREACH(rp, hp, rc_hash) {
	    if (newrp->rc_xid == rp->rc_xid &&
		newrp->rc_proc == rp->rc_proc &&
		(newrp->rc_flag & rp->rc_flag & RC_NFSVERS) &&
		nfsaddr_match(NETFAMILY(rp), &rp->rc_haddr, nd->nd_nam)) {
			if ((rp->rc_flag & RC_LOCKED) != 0) {
				rp->rc_flag |= RC_WANTED;
				(void)mtx_sleep(rp, mutex, (PZERO - 1) | PDROP,
				    "nfsrc", 10 * hz);
				goto loop;
			}
			if (rp->rc_flag == 0)
				panic("nfs udp cache0");
			rp->rc_flag |= RC_LOCKED;
			TAILQ_REMOVE(&nfsrvudplru, rp, rc_lru);
			TAILQ_INSERT_TAIL(&nfsrvudplru, rp, rc_lru);
			if (rp->rc_flag & RC_INPROG) {
				nfsstatsv1.srvcache_inproghits++;
				mtx_unlock(mutex);
				ret = RC_DROPIT;
			} else if (rp->rc_flag & RC_REPSTATUS) {
				/*
				 * V2 only.
				 */
				nfsstatsv1.srvcache_nonidemdonehits++;
				mtx_unlock(mutex);
				nfsrvd_rephead(nd);
				*(nd->nd_errp) = rp->rc_status;
				ret = RC_REPLY;
				rp->rc_timestamp = NFSD_MONOSEC +
					NFSRVCACHE_UDPTIMEOUT;
			} else if (rp->rc_flag & RC_REPMBUF) {
				nfsstatsv1.srvcache_nonidemdonehits++;
				mtx_unlock(mutex);
				nd->nd_mreq = m_copym(rp->rc_reply, 0,
					M_COPYALL, M_WAITOK);
				ret = RC_REPLY;
				rp->rc_timestamp = NFSD_MONOSEC +
					NFSRVCACHE_UDPTIMEOUT;
			} else {
				panic("nfs udp cache1");
			}
			nfsrc_unlock(rp);
			free(newrp, M_NFSRVCACHE);
			goto out;
		}
	}
	nfsstatsv1.srvcache_misses++;
	atomic_add_int(&nfsstatsv1.srvcache_size, 1);
	nfsrc_udpcachesize++;

	newrp->rc_flag |= RC_INPROG;
	saddr = NFSSOCKADDR(nd->nd_nam, struct sockaddr_in *);
	if (saddr->sin_family == AF_INET)
		newrp->rc_inet = saddr->sin_addr.s_addr;
	else if (saddr->sin_family == AF_INET6) {
		saddr6 = (struct sockaddr_in6 *)saddr;
		NFSBCOPY((caddr_t)&saddr6->sin6_addr, (caddr_t)&newrp->rc_inet6,
		    sizeof (struct in6_addr));
		newrp->rc_flag |= RC_INETIPV6;
	}
	LIST_INSERT_HEAD(hp, newrp, rc_hash);
	TAILQ_INSERT_TAIL(&nfsrvudplru, newrp, rc_lru);
	mtx_unlock(mutex);
	nd->nd_rp = newrp;
	ret = RC_DOIT;

out:
	NFSEXITCODE2(0, nd);
	return (ret);
}

/*
 * Update a request cache entry after the rpc has been done
 */
APPLESTATIC struct nfsrvcache *
nfsrvd_updatecache(struct nfsrv_descript *nd)
{
	struct nfsrvcache *rp;
	struct nfsrvcache *retrp = NULL;
	mbuf_t m;
	struct mtx *mutex;

	rp = nd->nd_rp;
	if (!rp)
		panic("nfsrvd_updatecache null rp");
	nd->nd_rp = NULL;
	mutex = nfsrc_cachemutex(rp);
	mtx_lock(mutex);
	nfsrc_lock(rp);
	if (!(rp->rc_flag & RC_INPROG))
		panic("nfsrvd_updatecache not inprog");
	rp->rc_flag &= ~RC_INPROG;
	if (rp->rc_flag & RC_UDP) {
		TAILQ_REMOVE(&nfsrvudplru, rp, rc_lru);
		TAILQ_INSERT_TAIL(&nfsrvudplru, rp, rc_lru);
	}

	/*
	 * Reply from cache is a special case returned by nfsrv_checkseqid().
	 */
	if (nd->nd_repstat == NFSERR_REPLYFROMCACHE) {
		nfsstatsv1.srvcache_nonidemdonehits++;
		mtx_unlock(mutex);
		nd->nd_repstat = 0;
		if (nd->nd_mreq)
			mbuf_freem(nd->nd_mreq);
		if (!(rp->rc_flag & RC_REPMBUF))
			panic("reply from cache");
		nd->nd_mreq = m_copym(rp->rc_reply, 0,
		    M_COPYALL, M_WAITOK);
		rp->rc_timestamp = NFSD_MONOSEC + nfsrc_tcptimeout;
		nfsrc_unlock(rp);
		goto out;
	}

	/*
	 * If rc_refcnt > 0, save it
	 * For UDP, save it if ND_SAVEREPLY is set
	 * For TCP, save it if ND_SAVEREPLY and nfsrc_tcpnonidempotent is set
	 */
	if (nd->nd_repstat != NFSERR_DONTREPLY &&
	    (rp->rc_refcnt > 0 ||
	     ((nd->nd_flag & ND_SAVEREPLY) && (rp->rc_flag & RC_UDP)) ||
	     ((nd->nd_flag & ND_SAVEREPLY) && !(rp->rc_flag & RC_UDP) &&
	      nfsrc_tcpsavedreplies <= nfsrc_floodlevel &&
	      nfsrc_tcpnonidempotent))) {
		if (rp->rc_refcnt > 0) {
			if (!(rp->rc_flag & RC_NFSV4))
				panic("update_cache refcnt");
			rp->rc_flag |= RC_REFCNT;
		}
		if ((nd->nd_flag & ND_NFSV2) &&
		    nfsv2_repstat[newnfsv2_procid[nd->nd_procnum]]) {
			rp->rc_status = nd->nd_repstat;
			rp->rc_flag |= RC_REPSTATUS;
			mtx_unlock(mutex);
		} else {
			if (!(rp->rc_flag & RC_UDP)) {
			    atomic_add_int(&nfsrc_tcpsavedreplies, 1);
			    if (nfsrc_tcpsavedreplies >
				nfsstatsv1.srvcache_tcppeak)
				nfsstatsv1.srvcache_tcppeak =
				    nfsrc_tcpsavedreplies;
			}
			mtx_unlock(mutex);
			m = m_copym(nd->nd_mreq, 0, M_COPYALL, M_WAITOK);
			mtx_lock(mutex);
			rp->rc_reply = m;
			rp->rc_flag |= RC_REPMBUF;
			mtx_unlock(mutex);
		}
		if (rp->rc_flag & RC_UDP) {
			rp->rc_timestamp = NFSD_MONOSEC +
			    NFSRVCACHE_UDPTIMEOUT;
			nfsrc_unlock(rp);
		} else {
			rp->rc_timestamp = NFSD_MONOSEC + nfsrc_tcptimeout;
			if (rp->rc_refcnt > 0)
				nfsrc_unlock(rp);
			else
				retrp = rp;
		}
	} else {
		nfsrc_freecache(rp);
		mtx_unlock(mutex);
	}

out:
	NFSEXITCODE2(0, nd);
	return (retrp);
}

/*
 * Invalidate and, if possible, free an in prog cache entry.
 * Must not sleep.
 */
APPLESTATIC void
nfsrvd_delcache(struct nfsrvcache *rp)
{
	struct mtx *mutex;

	mutex = nfsrc_cachemutex(rp);
	if (!(rp->rc_flag & RC_INPROG))
		panic("nfsrvd_delcache not in prog");
	mtx_lock(mutex);
	rp->rc_flag &= ~RC_INPROG;
	if (rp->rc_refcnt == 0 && !(rp->rc_flag & RC_LOCKED))
		nfsrc_freecache(rp);
	mtx_unlock(mutex);
}

/*
 * Called after nfsrvd_updatecache() once the reply is sent, to update
 * the entry's sequence number and unlock it. The argument is
 * the pointer returned by nfsrvd_updatecache().
 */
APPLESTATIC void
nfsrvd_sentcache(struct nfsrvcache *rp, int have_seq, uint32_t seq)
{
	struct nfsrchash_bucket *hbp;

	KASSERT(rp->rc_flag & RC_LOCKED, ("nfsrvd_sentcache not locked"));
	if (have_seq) {
		hbp = NFSRCAHASH(rp->rc_sockref);
		mtx_lock(&hbp->mtx);
		rp->rc_tcpseq = seq;
		if (rp->rc_acked != RC_NO_ACK)
			LIST_INSERT_HEAD(&hbp->tbl, rp, rc_ahash);
		rp->rc_acked = RC_NO_ACK;
		mtx_unlock(&hbp->mtx);
	}
	nfsrc_unlock(rp);
}

/*
 * Get a cache entry for TCP
 * - key on <xid, nfs version>
 *   (allow multiple entries for a given key)
 */
static int
nfsrc_gettcp(struct nfsrv_descript *nd, struct nfsrvcache *newrp)
{
	struct nfsrvcache *rp, *nextrp;
	int i;
	struct nfsrvcache *hitrp;
	struct nfsrvhashhead *hp, nfsrc_templist;
	int hit, ret = 0;
	struct mtx *mutex;

	mutex = nfsrc_cachemutex(newrp);
	hp = NFSRCHASH(newrp->rc_xid);
	newrp->rc_reqlen = nfsrc_getlenandcksum(nd->nd_mrep, &newrp->rc_cksum);
tryagain:
	mtx_lock(mutex);
	hit = 1;
	LIST_INIT(&nfsrc_templist);
	/*
	 * Get all the matches and put them on the temp list.
	 */
	rp = LIST_FIRST(hp);
	while (rp != LIST_END(hp)) {
		nextrp = LIST_NEXT(rp, rc_hash);
		if (newrp->rc_xid == rp->rc_xid &&
		    (!(rp->rc_flag & RC_INPROG) ||
		     ((newrp->rc_flag & RC_SAMETCPCONN) &&
		      newrp->rc_sockref == rp->rc_sockref)) &&
		    (newrp->rc_flag & rp->rc_flag & RC_NFSVERS) &&
		    newrp->rc_proc == rp->rc_proc &&
		    ((newrp->rc_flag & RC_NFSV4) &&
		     newrp->rc_sockref != rp->rc_sockref &&
		     newrp->rc_cachetime >= rp->rc_cachetime)
		    && newrp->rc_reqlen == rp->rc_reqlen &&
		    newrp->rc_cksum == rp->rc_cksum) {
			LIST_REMOVE(rp, rc_hash);
			LIST_INSERT_HEAD(&nfsrc_templist, rp, rc_hash);
		}
		rp = nextrp;
	}

	/*
	 * Now, use nfsrc_templist to decide if there is a match.
	 */
	i = 0;
	LIST_FOREACH(rp, &nfsrc_templist, rc_hash) {
		i++;
		if (rp->rc_refcnt > 0) {
			hit = 0;
			break;
		}
	}
	/*
	 * Can be a hit only if one entry left.
	 * Note possible hit entry and put nfsrc_templist back on hash
	 * list.
	 */
	if (i != 1)
		hit = 0;
	hitrp = rp = LIST_FIRST(&nfsrc_templist);
	while (rp != LIST_END(&nfsrc_templist)) {
		nextrp = LIST_NEXT(rp, rc_hash);
		LIST_REMOVE(rp, rc_hash);
		LIST_INSERT_HEAD(hp, rp, rc_hash);
		rp = nextrp;
	}
	if (LIST_FIRST(&nfsrc_templist) != LIST_END(&nfsrc_templist))
		panic("nfs gettcp cache templist");

	if (hit) {
		rp = hitrp;
		if ((rp->rc_flag & RC_LOCKED) != 0) {
			rp->rc_flag |= RC_WANTED;
			(void)mtx_sleep(rp, mutex, (PZERO - 1) | PDROP,
			    "nfsrc", 10 * hz);
			goto tryagain;
		}
		if (rp->rc_flag == 0)
			panic("nfs tcp cache0");
		rp->rc_flag |= RC_LOCKED;
		if (rp->rc_flag & RC_INPROG) {
			nfsstatsv1.srvcache_inproghits++;
			mtx_unlock(mutex);
			if (newrp->rc_sockref == rp->rc_sockref)
				nfsrc_marksametcpconn(rp->rc_sockref);
			ret = RC_DROPIT;
		} else if (rp->rc_flag & RC_REPSTATUS) {
			/*
			 * V2 only.
			 */
			nfsstatsv1.srvcache_nonidemdonehits++;
			mtx_unlock(mutex);
			if (newrp->rc_sockref == rp->rc_sockref)
				nfsrc_marksametcpconn(rp->rc_sockref);
			ret = RC_REPLY;
			nfsrvd_rephead(nd);
			*(nd->nd_errp) = rp->rc_status;
			rp->rc_timestamp = NFSD_MONOSEC + nfsrc_tcptimeout;
		} else if (rp->rc_flag & RC_REPMBUF) {
			nfsstatsv1.srvcache_nonidemdonehits++;
			mtx_unlock(mutex);
			if (newrp->rc_sockref == rp->rc_sockref)
				nfsrc_marksametcpconn(rp->rc_sockref);
			ret = RC_REPLY;
			nd->nd_mreq = m_copym(rp->rc_reply, 0,
				M_COPYALL, M_WAITOK);
			rp->rc_timestamp = NFSD_MONOSEC + nfsrc_tcptimeout;
		} else {
			panic("nfs tcp cache1");
		}
		nfsrc_unlock(rp);
		free(newrp, M_NFSRVCACHE);
		goto out;
	}
	nfsstatsv1.srvcache_misses++;
	atomic_add_int(&nfsstatsv1.srvcache_size, 1);

	/*
	 * For TCP, multiple entries for a key are allowed, so don't
	 * chain it into the hash table until done.
	 */
	newrp->rc_cachetime = NFSD_MONOSEC;
	newrp->rc_flag |= RC_INPROG;
	LIST_INSERT_HEAD(hp, newrp, rc_hash);
	mtx_unlock(mutex);
	nd->nd_rp = newrp;
	ret = RC_DOIT;

out:
	NFSEXITCODE2(0, nd);
	return (ret);
}

/*
 * Lock a cache entry.
 */
static void
nfsrc_lock(struct nfsrvcache *rp)
{
	struct mtx *mutex;

	mutex = nfsrc_cachemutex(rp);
	mtx_assert(mutex, MA_OWNED);
	while ((rp->rc_flag & RC_LOCKED) != 0) {
		rp->rc_flag |= RC_WANTED;
		(void)mtx_sleep(rp, mutex, PZERO - 1, "nfsrc", 0);
	}
	rp->rc_flag |= RC_LOCKED;
}

/*
 * Unlock a cache entry.
 */
static void
nfsrc_unlock(struct nfsrvcache *rp)
{
	struct mtx *mutex;

	mutex = nfsrc_cachemutex(rp);
	mtx_lock(mutex);
	rp->rc_flag &= ~RC_LOCKED;
	nfsrc_wanted(rp);
	mtx_unlock(mutex);
}

/*
 * Wakeup anyone wanting entry.
 */
static void
nfsrc_wanted(struct nfsrvcache *rp)
{
	if (rp->rc_flag & RC_WANTED) {
		rp->rc_flag &= ~RC_WANTED;
		wakeup((caddr_t)rp);
	}
}

/*
 * Free up the entry.
 * Must not sleep.
 */
static void
nfsrc_freecache(struct nfsrvcache *rp)
{
	struct nfsrchash_bucket *hbp;

	LIST_REMOVE(rp, rc_hash);
	if (rp->rc_flag & RC_UDP) {
		TAILQ_REMOVE(&nfsrvudplru, rp, rc_lru);
		nfsrc_udpcachesize--;
	} else if (rp->rc_acked != RC_NO_SEQ) {
		hbp = NFSRCAHASH(rp->rc_sockref);
		mtx_lock(&hbp->mtx);
		if (rp->rc_acked == RC_NO_ACK)
			LIST_REMOVE(rp, rc_ahash);
		mtx_unlock(&hbp->mtx);
	}
	nfsrc_wanted(rp);
	if (rp->rc_flag & RC_REPMBUF) {
		mbuf_freem(rp->rc_reply);
		if (!(rp->rc_flag & RC_UDP))
			atomic_add_int(&nfsrc_tcpsavedreplies, -1);
	}
	free(rp, M_NFSRVCACHE);
	atomic_add_int(&nfsstatsv1.srvcache_size, -1);
}

/*
 * Clean out the cache. Called when nfsserver module is unloaded.
 */
APPLESTATIC void
nfsrvd_cleancache(void)
{
	struct nfsrvcache *rp, *nextrp;
	int i;

	for (i = 0; i < NFSRVCACHE_HASHSIZE; i++) {
		mtx_lock(&nfsrchash_table[i].mtx);
		LIST_FOREACH_SAFE(rp, &nfsrchash_table[i].tbl, rc_hash, nextrp)
			nfsrc_freecache(rp);
		mtx_unlock(&nfsrchash_table[i].mtx);
	}
	mtx_lock(&nfsrc_udpmtx);
	for (i = 0; i < NFSRVCACHE_HASHSIZE; i++) {
		LIST_FOREACH_SAFE(rp, &nfsrvudphashtbl[i], rc_hash, nextrp) {
			nfsrc_freecache(rp);
		}
	}
	nfsstatsv1.srvcache_size = 0;
	mtx_unlock(&nfsrc_udpmtx);
	nfsrc_tcpsavedreplies = 0;
}

#define HISTSIZE	16
/*
 * The basic rule is to get rid of entries that are expired.
 */
void
nfsrc_trimcache(u_int64_t sockref, uint32_t snd_una, int final)
{
	struct nfsrchash_bucket *hbp;
	struct nfsrvcache *rp, *nextrp;
	int force, lastslot, i, j, k, tto, time_histo[HISTSIZE];
	time_t thisstamp;
	static time_t udp_lasttrim = 0, tcp_lasttrim = 0;
	static int onethread = 0, oneslot = 0;

	if (sockref != 0) {
		hbp = NFSRCAHASH(sockref);
		mtx_lock(&hbp->mtx);
		LIST_FOREACH_SAFE(rp, &hbp->tbl, rc_ahash, nextrp) {
			if (sockref == rp->rc_sockref) {
				if (SEQ_GEQ(snd_una, rp->rc_tcpseq)) {
					rp->rc_acked = RC_ACK;
					LIST_REMOVE(rp, rc_ahash);
				} else if (final) {
					rp->rc_acked = RC_NACK;
					LIST_REMOVE(rp, rc_ahash);
				}
			}
		}
		mtx_unlock(&hbp->mtx);
	}

	if (atomic_cmpset_acq_int(&onethread, 0, 1) == 0)
		return;
	if (NFSD_MONOSEC != udp_lasttrim ||
	    nfsrc_udpcachesize >= (nfsrc_udphighwater +
	    nfsrc_udphighwater / 2)) {
		mtx_lock(&nfsrc_udpmtx);
		udp_lasttrim = NFSD_MONOSEC;
		TAILQ_FOREACH_SAFE(rp, &nfsrvudplru, rc_lru, nextrp) {
			if (!(rp->rc_flag & (RC_INPROG|RC_LOCKED|RC_WANTED))
			     && rp->rc_refcnt == 0
			     && ((rp->rc_flag & RC_REFCNT) ||
				 udp_lasttrim > rp->rc_timestamp ||
				 nfsrc_udpcachesize > nfsrc_udphighwater))
				nfsrc_freecache(rp);
		}
		mtx_unlock(&nfsrc_udpmtx);
	}
	if (NFSD_MONOSEC != tcp_lasttrim ||
	    nfsrc_tcpsavedreplies >= nfsrc_tcphighwater) {
		force = nfsrc_tcphighwater / 4;
		if (force > 0 &&
		    nfsrc_tcpsavedreplies + force >= nfsrc_tcphighwater) {
			for (i = 0; i < HISTSIZE; i++)
				time_histo[i] = 0;
			i = 0;
			lastslot = NFSRVCACHE_HASHSIZE - 1;
		} else {
			force = 0;
			if (NFSD_MONOSEC != tcp_lasttrim) {
				i = 0;
				lastslot = NFSRVCACHE_HASHSIZE - 1;
			} else {
				lastslot = i = oneslot;
				if (++oneslot >= NFSRVCACHE_HASHSIZE)
					oneslot = 0;
			}
		}
		tto = nfsrc_tcptimeout;
		tcp_lasttrim = NFSD_MONOSEC;
		for (; i <= lastslot; i++) {
			mtx_lock(&nfsrchash_table[i].mtx);
			LIST_FOREACH_SAFE(rp, &nfsrchash_table[i].tbl, rc_hash,
			    nextrp) {
				if (!(rp->rc_flag &
				     (RC_INPROG|RC_LOCKED|RC_WANTED))
				     && rp->rc_refcnt == 0) {
					if ((rp->rc_flag & RC_REFCNT) ||
					    tcp_lasttrim > rp->rc_timestamp ||
					    rp->rc_acked == RC_ACK) {
						nfsrc_freecache(rp);
						continue;
					}

					if (force == 0)
						continue;
					/*
					 * The timestamps range from roughly the
					 * present (tcp_lasttrim) to the present
					 * + nfsrc_tcptimeout. Generate a simple
					 * histogram of where the timeouts fall.
					 */
					j = rp->rc_timestamp - tcp_lasttrim;
					if (j >= tto)
						j = HISTSIZE - 1;
					else if (j < 0)
						j = 0;
					else
						j = j * HISTSIZE / tto;
					time_histo[j]++;
				}
			}
			mtx_unlock(&nfsrchash_table[i].mtx);
		}
		if (force) {
			/*
			 * Trim some more with a smaller timeout of as little
			 * as 20% of nfsrc_tcptimeout to try and get below
			 * 80% of the nfsrc_tcphighwater.
			 */
			k = 0;
			for (i = 0; i < (HISTSIZE - 2); i++) {
				k += time_histo[i];
				if (k > force)
					break;
			}
			k = tto * (i + 1) / HISTSIZE;
			if (k < 1)
				k = 1;
			thisstamp = tcp_lasttrim + k;
			for (i = 0; i < NFSRVCACHE_HASHSIZE; i++) {
				mtx_lock(&nfsrchash_table[i].mtx);
				LIST_FOREACH_SAFE(rp, &nfsrchash_table[i].tbl,
				    rc_hash, nextrp) {
					if (!(rp->rc_flag &
					     (RC_INPROG|RC_LOCKED|RC_WANTED))
					     && rp->rc_refcnt == 0
					     && ((rp->rc_flag & RC_REFCNT) ||
						 thisstamp > rp->rc_timestamp ||
						 rp->rc_acked == RC_ACK))
						nfsrc_freecache(rp);
				}
				mtx_unlock(&nfsrchash_table[i].mtx);
			}
		}
	}
	atomic_store_rel_int(&onethread, 0);
}

/*
 * Add a seqid# reference to the cache entry.
 */
APPLESTATIC void
nfsrvd_refcache(struct nfsrvcache *rp)
{
	struct mtx *mutex;

	if (rp == NULL)
		/* For NFSv4.1, there is no cache entry. */
		return;
	mutex = nfsrc_cachemutex(rp);
	mtx_lock(mutex);
	if (rp->rc_refcnt < 0)
		panic("nfs cache refcnt");
	rp->rc_refcnt++;
	mtx_unlock(mutex);
}

/*
 * Dereference a seqid# cache entry.
 */
APPLESTATIC void
nfsrvd_derefcache(struct nfsrvcache *rp)
{
	struct mtx *mutex;

	mutex = nfsrc_cachemutex(rp);
	mtx_lock(mutex);
	if (rp->rc_refcnt <= 0)
		panic("nfs cache derefcnt");
	rp->rc_refcnt--;
	if (rp->rc_refcnt == 0 && !(rp->rc_flag & (RC_LOCKED | RC_INPROG)))
		nfsrc_freecache(rp);
	mtx_unlock(mutex);
}

/*
 * Calculate the length of the mbuf list and a checksum on the first up to
 * NFSRVCACHE_CHECKLEN bytes.
 */
static int
nfsrc_getlenandcksum(mbuf_t m1, u_int16_t *cksum)
{
	int len = 0, cklen;
	mbuf_t m;

	m = m1;
	while (m) {
		len += mbuf_len(m);
		m = mbuf_next(m);
	}
	cklen = (len > NFSRVCACHE_CHECKLEN) ? NFSRVCACHE_CHECKLEN : len;
	*cksum = in_cksum(m1, cklen);
	return (len);
}

/*
 * Mark a TCP connection that is seeing retries. Should never happen for
 * NFSv4.
 */
static void
nfsrc_marksametcpconn(u_int64_t sockref)
{
}

