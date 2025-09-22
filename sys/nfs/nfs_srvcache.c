/*	$OpenBSD: nfs_srvcache.c,v 1.32 2024/09/18 05:21:19 jsg Exp $	*/
/*	$NetBSD: nfs_srvcache.c,v 1.12 1996/02/18 11:53:49 fvdl Exp $	*/

/*
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
 *	@(#)nfs_srvcache.c	8.3 (Berkeley) 3/30/95
 */

/*
 * Reference: Chet Juszczak, "Improving the Performance and Correctness
 *		of an NFS Server", in Proc. Winter 1989 USENIX Conference,
 *		pages 53-63. San Diego, February 1989.
 */
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <crypto/siphash.h>

#include <netinet/in.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsrvcache.h>
#include <nfs/nfs_var.h>

extern struct nfsstats nfsstats;
extern const int nfsv2_procid[NFS_NPROCS];
long numnfsrvcache, desirednfsrvcache = NFSRVCACHESIZ;

struct nfsrvcache	*nfsrv_lookupcache(struct nfsrv_descript *);
void			 nfsrv_cleanentry(struct nfsrvcache *);

LIST_HEAD(nfsrvhash, nfsrvcache) *nfsrvhashtbl;
SIPHASH_KEY nfsrvhashkey;
TAILQ_HEAD(nfsrvlru, nfsrvcache) nfsrvlruhead;
u_long nfsrvhash;
#define	NFSRCHASH(xid) \
    (&nfsrvhashtbl[SipHash24(&nfsrvhashkey, &(xid), sizeof(xid)) & nfsrvhash])

#define	NETFAMILY(rp)	\
	(((rp)->rc_flag & RC_INETADDR) ? AF_INET : AF_UNSPEC)

/* Array that defines which nfs rpc's are nonidempotent */
static const int nonidempotent[NFS_NPROCS] = {
	0, 0, 1, 0, 0, 0, 0, 1,
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0
};

/* True iff the rpc reply is an nfs status ONLY! */
static const int nfsv2_repstat[NFS_NPROCS] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 1, 1, 1, 1, 0, 1,
	0, 0
};

void
nfsrv_cleanentry(struct nfsrvcache *rp)
{
	if ((rp->rc_flag & RC_REPMBUF) != 0)
		m_freem(rp->rc_reply);

	if ((rp->rc_flag & RC_NAM) != 0)
		m_free(rp->rc_nam);

	rp->rc_flag &= ~(RC_REPSTATUS|RC_REPMBUF);
}

/* Initialize the server request cache list */
void
nfsrv_initcache(void)
{

	nfsrvhashtbl = hashinit(desirednfsrvcache, M_NFSD, M_WAITOK, &nfsrvhash);
	arc4random_buf(&nfsrvhashkey, sizeof(nfsrvhashkey));
	TAILQ_INIT(&nfsrvlruhead);
}

/*
 * Look for the request in the cache
 * If found then
 *    return action and optionally reply
 * else
 *    insert it in the cache
 *
 * The rules are as follows:
 * - if in progress, return DROP request
 * - if completed within DELAY of the current time, return DROP it
 * - if completed a longer time ago return REPLY if the reply was cached or
 *   return DOIT
 * Update/add new request at end of lru list
 */
int
nfsrv_getcache(struct nfsrv_descript *nd, struct nfssvc_sock *slp,
    struct mbuf **repp)
{
	struct nfsrvhash *hash;
	struct nfsrvcache *rp;
	struct mbuf *mb;
	struct sockaddr_in *saddr;
	int ret;

	/*
	 * Don't cache recent requests for reliable transport protocols.
	 * (Maybe we should for the case of a reconnect, but..)
	 */
	if (!nd->nd_nam2)
		return (RC_DOIT);

	rp = nfsrv_lookupcache(nd);
	if (rp) {
		/* If not at end of LRU chain, move it there */
		if (TAILQ_NEXT(rp, rc_lru)) {
			TAILQ_REMOVE(&nfsrvlruhead, rp, rc_lru);
			TAILQ_INSERT_TAIL(&nfsrvlruhead, rp, rc_lru);
		}
		if (rp->rc_state == RC_UNUSED)
			panic("nfsrv cache");
		if (rp->rc_state == RC_INPROG) {
			nfsstats.srvcache_inproghits++;
			ret = RC_DROPIT;
		} else if (rp->rc_flag & RC_REPSTATUS) {
			nfsstats.srvcache_nonidemdonehits++;
			nfs_rephead(0, nd, slp, rp->rc_status, repp, &mb);
			ret = RC_REPLY;
		} else if (rp->rc_flag & RC_REPMBUF) {
			nfsstats.srvcache_nonidemdonehits++;
			*repp = m_copym(rp->rc_reply, 0, M_COPYALL, M_WAIT);
			ret = RC_REPLY;
		} else {
			nfsstats.srvcache_idemdonehits++;
			rp->rc_state = RC_INPROG;
			ret = RC_DOIT;
		}
		rp->rc_flag &= ~RC_LOCKED;
		if (rp->rc_flag & RC_WANTED) {
			rp->rc_flag &= ~RC_WANTED;
			wakeup(rp);
		}
		return (ret);
	}

	nfsstats.srvcache_misses++;
	if (numnfsrvcache < desirednfsrvcache) {
		rp = malloc(sizeof(*rp), M_NFSD, M_WAITOK|M_ZERO);
		numnfsrvcache++;
		rp->rc_flag = RC_LOCKED;
	} else {
		rp = TAILQ_FIRST(&nfsrvlruhead);
		while ((rp->rc_flag & RC_LOCKED) != 0) {
			rp->rc_flag |= RC_WANTED;
			tsleep_nsec(rp, PZERO-1, "nfsrc", INFSLP);
			rp = TAILQ_FIRST(&nfsrvlruhead);
		}
		rp->rc_flag |= RC_LOCKED;
		LIST_REMOVE(rp, rc_hash);
		TAILQ_REMOVE(&nfsrvlruhead, rp, rc_lru);
		nfsrv_cleanentry(rp);
		rp->rc_flag &= (RC_LOCKED | RC_WANTED);
	}
	TAILQ_INSERT_TAIL(&nfsrvlruhead, rp, rc_lru);
	rp->rc_state = RC_INPROG;
	rp->rc_xid = nd->nd_retxid;
	saddr = mtod(nd->nd_nam, struct sockaddr_in *);
	switch (saddr->sin_family) {
	case AF_INET:
		rp->rc_flag |= RC_INETADDR;
		rp->rc_inetaddr = saddr->sin_addr.s_addr;
		break;
	default:
		rp->rc_flag |= RC_NAM;
		rp->rc_nam = m_copym(nd->nd_nam, 0, M_COPYALL, M_WAIT);
		break;
	}
	rp->rc_proc = nd->nd_procnum;
	hash = NFSRCHASH(nd->nd_retxid);
	LIST_INSERT_HEAD(hash, rp, rc_hash);
	rp->rc_flag &= ~RC_LOCKED;
	if (rp->rc_flag & RC_WANTED) {
		rp->rc_flag &= ~RC_WANTED;
		wakeup(rp);
	}
	return (RC_DOIT);
}

/* Update a request cache entry after the rpc has been done */
void
nfsrv_updatecache(struct nfsrv_descript *nd, int repvalid,
    struct mbuf *repmbuf)
{
	struct nfsrvcache *rp;

	if (!nd->nd_nam2)
		return;

	rp = nfsrv_lookupcache(nd);
	if (rp) {
		nfsrv_cleanentry(rp);
		rp->rc_state = RC_DONE;
		/*
		 * If we have a valid reply update status and save
		 * the reply for non-idempotent rpc's.
		 */
		if (repvalid && nonidempotent[nd->nd_procnum]) {
			if ((nd->nd_flag & ND_NFSV3) == 0 &&
			  nfsv2_repstat[nfsv2_procid[nd->nd_procnum]]) {
				rp->rc_status = nd->nd_repstat;
				rp->rc_flag |= RC_REPSTATUS;
			} else {
				rp->rc_reply = m_copym(repmbuf, 0, M_COPYALL,
				    M_WAIT);
				rp->rc_flag |= RC_REPMBUF;
			}
		}
		rp->rc_flag &= ~RC_LOCKED;
		if (rp->rc_flag & RC_WANTED) {
			rp->rc_flag &= ~RC_WANTED;
			wakeup(rp);
		}
		return;
	}
}

/* Clean out the cache. Called when the last nfsd terminates. */
void
nfsrv_cleancache(void)
{
	struct nfsrvcache *rp, *nextrp;

	for (rp = TAILQ_FIRST(&nfsrvlruhead); rp != NULL; rp = nextrp) {
		nextrp = TAILQ_NEXT(rp, rc_lru);
		LIST_REMOVE(rp, rc_hash);
		TAILQ_REMOVE(&nfsrvlruhead, rp, rc_lru);
		nfsrv_cleanentry(rp);
		free(rp, M_NFSD, sizeof(*rp));
	}
	numnfsrvcache = 0;
}

struct nfsrvcache *
nfsrv_lookupcache(struct nfsrv_descript *nd)
{
	struct nfsrvhash	*hash;
	struct nfsrvcache	*rp;

	hash = NFSRCHASH(nd->nd_retxid);
loop:
	LIST_FOREACH(rp, hash, rc_hash) {
		if (nd->nd_retxid == rp->rc_xid &&
		    nd->nd_procnum == rp->rc_proc &&
		    netaddr_match(NETFAMILY(rp), &rp->rc_haddr, nd->nd_nam)) {
			if ((rp->rc_flag & RC_LOCKED)) {
				rp->rc_flag |= RC_WANTED;
				tsleep_nsec(rp, PZERO - 1, "nfsrc", INFSLP);
				goto loop;
			}
			rp->rc_flag |= RC_LOCKED;
			return (rp);
		}
	}

	return (NULL);
}
