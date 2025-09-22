/*	$OpenBSD: vfs_cache.c,v 1.58 2022/08/14 01:58:28 jsg Exp $	*/
/*	$NetBSD: vfs_cache.c,v 1.13 1996/02/04 02:18:09 christos Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)vfs_cache.c	8.3 (Berkeley) 8/22/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/namei.h>
#include <sys/errno.h>
#include <sys/pool.h>

/*
 * TODO: namecache access should really be locked.
 */

/*
 * For simplicity (and economy of storage), names longer than
 * a maximum length of NAMECACHE_MAXLEN are not cached; they occur
 * infrequently in any case, and are almost never of interest.
 *
 * Upon reaching the last segment of a path, if the reference
 * is for DELETE, or NOCACHE is set (rewrite), and the
 * name is located in the cache, it will be dropped.
 */

/*
 * Structures associated with name caching.
 */
long	numcache;	/* total number of cache entries allocated */
long	numneg;		/* number of negative cache entries */

TAILQ_HEAD(, namecache) nclruhead;	/* Regular Entry LRU chain */
TAILQ_HEAD(, namecache) nclruneghead;	/* Negative Entry LRU chain */
struct	nchstats nchstats;		/* cache effectiveness statistics */

int doingcache = 1;			/* 1 => enable the cache */

struct pool nch_pool;

void cache_zap(struct namecache *);
u_long nextvnodeid;

static inline int
namecache_compare(const struct namecache *n1, const struct namecache *n2)
{
	if (n1->nc_nlen == n2->nc_nlen)
		return (memcmp(n1->nc_name, n2->nc_name, n1->nc_nlen));
	else
		return (n1->nc_nlen - n2->nc_nlen);
}

RBT_PROTOTYPE(namecache_rb_cache, namecache, n_rbcache, namecache_compare);
RBT_GENERATE(namecache_rb_cache, namecache, n_rbcache, namecache_compare);

void
cache_tree_init(struct namecache_rb_cache *tree)
{
	RBT_INIT(namecache_rb_cache, tree);
}

/*
 * blow away a namecache entry
 */
void
cache_zap(struct namecache *ncp)
{
	struct vnode *dvp = NULL;

	if (ncp->nc_vp != NULL) {
		TAILQ_REMOVE(&nclruhead, ncp, nc_lru);
		numcache--;
	} else {
		TAILQ_REMOVE(&nclruneghead, ncp, nc_neg);
		numneg--;
	}
	if (ncp->nc_dvp) {
		RBT_REMOVE(namecache_rb_cache, &ncp->nc_dvp->v_nc_tree, ncp);
		if (RBT_EMPTY(namecache_rb_cache, &ncp->nc_dvp->v_nc_tree))
			dvp = ncp->nc_dvp;
	}
	if (ncp->nc_vp && (ncp->nc_vpid == ncp->nc_vp->v_id)) {
		if (ncp->nc_vp != ncp->nc_dvp &&
		    ncp->nc_vp->v_type == VDIR &&
		    (ncp->nc_nlen > 2 ||
			(ncp->nc_nlen > 1 &&
			    ncp->nc_name[1] != '.') ||
			(ncp->nc_nlen > 0 &&
			    ncp->nc_name[0] != '.'))) {
			TAILQ_REMOVE(&ncp->nc_vp->v_cache_dst, ncp, nc_me);
		}
	}
	pool_put(&nch_pool, ncp);
	if (dvp)
		vdrop(dvp);
}

/*
 * Look for a name in the cache.
 * dvp points to the directory to search. The componentname cnp holds
 * the information on the entry being sought, such as its length
 * and its name. If the lookup succeeds, vpp is set to point to the vnode
 * and an error of 0 is returned. If the lookup determines the name does
 * not exist (negative caching) an error of ENOENT is returned. If the
 * lookup fails, an error of -1 is returned.
 */
int
cache_lookup(struct vnode *dvp, struct vnode **vpp,
    struct componentname *cnp)
{
	struct namecache *ncp;
	struct namecache n;
	struct vnode *vp;
	u_long vpid;
	int error;

	*vpp = NULL;

	if (!doingcache) {
		cnp->cn_flags &= ~MAKEENTRY;
		return (-1);
	}
	if (cnp->cn_namelen > NAMECACHE_MAXLEN) {
		nchstats.ncs_long++;
		cnp->cn_flags &= ~MAKEENTRY;
		return (-1);
	}

	/* lookup in directory vnode's redblack tree */
	n.nc_nlen = cnp->cn_namelen;
	memcpy(n.nc_name, cnp->cn_nameptr, n.nc_nlen);
	ncp = RBT_FIND(namecache_rb_cache, &dvp->v_nc_tree, &n);

	if (ncp == NULL) {
		nchstats.ncs_miss++;
		return (-1);
	}
	if ((cnp->cn_flags & MAKEENTRY) == 0) {
		nchstats.ncs_badhits++;
		goto remove;
	} else if (ncp->nc_vp == NULL) {
		if (cnp->cn_nameiop != CREATE ||
		    (cnp->cn_flags & ISLASTCN) == 0) {
			nchstats.ncs_neghits++;
			/*
			 * Move this slot to end of the negative LRU chain,
			 */
			if (TAILQ_NEXT(ncp, nc_neg) != NULL) {
				TAILQ_REMOVE(&nclruneghead, ncp, nc_neg);
				TAILQ_INSERT_TAIL(&nclruneghead, ncp,
				    nc_neg);
			}
			return (ENOENT);
		} else {
			nchstats.ncs_badhits++;
			goto remove;
		}
	} else if (ncp->nc_vpid != ncp->nc_vp->v_id) {
		nchstats.ncs_falsehits++;
		goto remove;
	}

	/*
	 * Move this slot to end of the regular LRU chain.
	 */
	if (TAILQ_NEXT(ncp, nc_lru) != NULL) {
		TAILQ_REMOVE(&nclruhead, ncp, nc_lru);
		TAILQ_INSERT_TAIL(&nclruhead, ncp, nc_lru);
	}

	vp = ncp->nc_vp;
	vpid = vp->v_id;
	if (vp == dvp) {	/* lookup on "." */
		vref(dvp);
		error = 0;
	} else if (cnp->cn_flags & ISDOTDOT) {
		VOP_UNLOCK(dvp);
		cnp->cn_flags |= PDIRUNLOCK;
		error = vget(vp, LK_EXCLUSIVE);
		/*
		 * If the above vget() succeeded and both LOCKPARENT and
		 * ISLASTCN is set, lock the directory vnode as well.
		 */
		if (!error && (~cnp->cn_flags & (LOCKPARENT|ISLASTCN)) == 0) {
			if ((error = vn_lock(dvp, LK_EXCLUSIVE)) != 0) {
				vput(vp);
				return (error);
			}
			cnp->cn_flags &= ~PDIRUNLOCK;
		}
	} else {
		error = vget(vp, LK_EXCLUSIVE);
		/*
		 * If the above vget() failed or either of LOCKPARENT or
		 * ISLASTCN is set, unlock the directory vnode.
		 */
		if (error || (~cnp->cn_flags & (LOCKPARENT|ISLASTCN)) != 0) {
			VOP_UNLOCK(dvp);
			cnp->cn_flags |= PDIRUNLOCK;
		}
	}

	/*
	 * Check that the lock succeeded, and that the capability number did
	 * not change while we were waiting for the lock.
	 */
	if (error || vpid != vp->v_id) {
		if (!error) {
			vput(vp);
			nchstats.ncs_falsehits++;
		} else
			nchstats.ncs_badhits++;
		/*
		 * The parent needs to be locked when we return to VOP_LOOKUP().
		 * The `.' case here should be extremely rare (if it can happen
		 * at all), so we don't bother optimizing out the unlock/relock.
		 */
		if (vp == dvp || error ||
		    (~cnp->cn_flags & (LOCKPARENT|ISLASTCN)) != 0) {
			if ((error = vn_lock(dvp, LK_EXCLUSIVE)) != 0)
				return (error);
			cnp->cn_flags &= ~PDIRUNLOCK;
		}
		return (-1);
	}

	nchstats.ncs_goodhits++;
	*vpp = vp;
	return (0);

remove:
	/*
	 * Last component and we are renaming or deleting,
	 * the cache entry is invalid, or otherwise don't
	 * want cache entry to exist.
	 */
	cache_zap(ncp);
	return (-1);
}

/*
 * Scan cache looking for name of directory entry pointing at vp.
 *
 * Fill in dvpp.
 *
 * If bufp is non-NULL, also place the name in the buffer which starts
 * at bufp, immediately before *bpp, and move bpp backwards to point
 * at the start of it.  (Yes, this is a little baroque, but it's done
 * this way to cater to the whims of getcwd).
 *
 * Returns 0 on success, -1 on cache miss, positive errno on failure.
 *
 * TODO: should we return *dvpp locked?
 */

int
cache_revlookup(struct vnode *vp, struct vnode **dvpp, char **bpp, char *bufp)
{
	struct namecache *ncp;
	struct vnode *dvp = NULL;
	char *bp;

	if (!doingcache)
		goto out;
	TAILQ_FOREACH(ncp, &vp->v_cache_dst, nc_me) {
		dvp = ncp->nc_dvp;
		if (dvp && dvp != vp && ncp->nc_dvpid == dvp->v_id)
			goto found;
	}
	goto miss;
found:
#ifdef DIAGNOSTIC
	if (ncp->nc_nlen == 1 &&
	    ncp->nc_name[0] == '.')
		panic("cache_revlookup: found entry for .");
	if (ncp->nc_nlen == 2 &&
	    ncp->nc_name[0] == '.' &&
	    ncp->nc_name[1] == '.')
		panic("cache_revlookup: found entry for ..");
#endif
	nchstats.ncs_revhits++;

	if (bufp != NULL) {
		bp = *bpp;
		bp -= ncp->nc_nlen;
		if (bp <= bufp) {
			*dvpp = NULL;
			return (ERANGE);
		}
		memcpy(bp, ncp->nc_name, ncp->nc_nlen);
		*bpp = bp;
	}

	*dvpp = dvp;

	/*
	 * XXX: Should we vget() here to have more
	 * consistent semantics with cache_lookup()?
	 */
	return (0);

miss:
	nchstats.ncs_revmiss++;
out:
	*dvpp = NULL;
	return (-1);
}

/*
 * Add an entry to the cache
 */
void
cache_enter(struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
	struct namecache *ncp, *lncp;

	if (!doingcache || cnp->cn_namelen > NAMECACHE_MAXLEN)
		return;

	/*
	 * allocate, or recycle (free and allocate) an ncp.
	 */
	if (numcache >= initialvnodes) {
		if ((ncp = TAILQ_FIRST(&nclruhead)) != NULL)
			cache_zap(ncp);
		else if ((ncp = TAILQ_FIRST(&nclruneghead)) != NULL)
			cache_zap(ncp);
		else
			panic("wtf? leak?");
	}
	ncp = pool_get(&nch_pool, PR_WAITOK|PR_ZERO);

	/* grab the vnode we just found */
	ncp->nc_vp = vp;
	if (vp)
		ncp->nc_vpid = vp->v_id;

	/* fill in cache info */
	ncp->nc_dvp = dvp;
	ncp->nc_dvpid = dvp->v_id;
	ncp->nc_nlen = cnp->cn_namelen;
	memcpy(ncp->nc_name, cnp->cn_nameptr, ncp->nc_nlen);
	if (RBT_EMPTY(namecache_rb_cache, &dvp->v_nc_tree)) {
		vhold(dvp);
	}
	if ((lncp = RBT_INSERT(namecache_rb_cache, &dvp->v_nc_tree, ncp))
	    != NULL) {
		/* someone has raced us and added a different entry
		 * for the same vnode (different ncp) - we don't need
		 * this entry, so free it and we are done.
		 */
		pool_put(&nch_pool, ncp);
		/* we know now dvp->v_nc_tree is not empty, no need
		 * to vdrop here
		 */
		goto done;
	}
	if (vp) {
		TAILQ_INSERT_TAIL(&nclruhead, ncp, nc_lru);
		numcache++;
		/* don't put . or .. in the reverse map */
		if (vp != dvp && vp->v_type == VDIR &&
		    (ncp->nc_nlen > 2 ||
			(ncp->nc_nlen > 1 &&
			    ncp->nc_name[1] != '.') ||
			(ncp->nc_nlen > 0 &&
			    ncp->nc_name[0] != '.')))
			TAILQ_INSERT_TAIL(&vp->v_cache_dst, ncp,
			    nc_me);
	} else {
		TAILQ_INSERT_TAIL(&nclruneghead, ncp, nc_neg);
		numneg++;
	}
	if (numneg  > initialvnodes) {
		if ((ncp = TAILQ_FIRST(&nclruneghead))
		    != NULL)
			cache_zap(ncp);
	}
done:
	return;
}


/*
 * Name cache initialization, from vfs_init() when we are booting
 */
void
nchinit(void)
{
	TAILQ_INIT(&nclruhead);
	TAILQ_INIT(&nclruneghead);
	pool_init(&nch_pool, sizeof(struct namecache), 0, IPL_NONE, PR_WAITOK,
	    "nchpl", NULL);
}

/*
 * Cache flush, a particular vnode; called when a vnode is renamed to
 * hide entries that would now be invalid
 */
void
cache_purge(struct vnode *vp)
{
	struct namecache *ncp;

	/* We should never have destinations cached for a non-VDIR vnode. */
	KASSERT(vp->v_type == VDIR || TAILQ_EMPTY(&vp->v_cache_dst));

	while ((ncp = TAILQ_FIRST(&vp->v_cache_dst)))
		cache_zap(ncp);
	while ((ncp = RBT_ROOT(namecache_rb_cache, &vp->v_nc_tree)))
		cache_zap(ncp);

	/* XXX this blows goats */
	vp->v_id = ++nextvnodeid;
	if (vp->v_id == 0)
		vp->v_id = ++nextvnodeid;
}

/*
 * Cache flush, a whole filesystem; called when filesys is umounted to
 * remove entries that would now be invalid
 */
void
cache_purgevfs(struct mount *mp)
{
	struct namecache *ncp, *nxtcp;

	/* whack the regular entries */
	TAILQ_FOREACH_SAFE(ncp, &nclruhead, nc_lru, nxtcp) {
		if (ncp->nc_dvp == NULL || ncp->nc_dvp->v_mount != mp)
			continue;
		/* free the resources we had */
		cache_zap(ncp);
	}
	/* whack the negative entries */
	TAILQ_FOREACH_SAFE(ncp, &nclruneghead, nc_neg, nxtcp) {
		if (ncp->nc_dvp == NULL || ncp->nc_dvp->v_mount != mp)
			continue;
		/* free the resources we had */
		cache_zap(ncp);
	}
}
