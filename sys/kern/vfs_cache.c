/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Poul-Henning Kamp of the FreeBSD Project.
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
 *	@(#)vfs_cache.c	8.5 (Berkeley) 3/22/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/filedesc.h>
#include <sys/fnv_hash.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sdt.h>
#include <sys/smp.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/vnode.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <vm/uma.h>

SDT_PROVIDER_DECLARE(vfs);
SDT_PROBE_DEFINE3(vfs, namecache, enter, done, "struct vnode *", "char *",
    "struct vnode *");
SDT_PROBE_DEFINE2(vfs, namecache, enter_negative, done, "struct vnode *",
    "char *");
SDT_PROBE_DEFINE1(vfs, namecache, fullpath, entry, "struct vnode *");
SDT_PROBE_DEFINE3(vfs, namecache, fullpath, hit, "struct vnode *",
    "char *", "struct vnode *");
SDT_PROBE_DEFINE1(vfs, namecache, fullpath, miss, "struct vnode *");
SDT_PROBE_DEFINE3(vfs, namecache, fullpath, return, "int",
    "struct vnode *", "char *");
SDT_PROBE_DEFINE3(vfs, namecache, lookup, hit, "struct vnode *", "char *",
    "struct vnode *");
SDT_PROBE_DEFINE2(vfs, namecache, lookup, hit__negative,
    "struct vnode *", "char *");
SDT_PROBE_DEFINE2(vfs, namecache, lookup, miss, "struct vnode *",
    "char *");
SDT_PROBE_DEFINE1(vfs, namecache, purge, done, "struct vnode *");
SDT_PROBE_DEFINE1(vfs, namecache, purge_negative, done, "struct vnode *");
SDT_PROBE_DEFINE1(vfs, namecache, purgevfs, done, "struct mount *");
SDT_PROBE_DEFINE3(vfs, namecache, zap, done, "struct vnode *", "char *",
    "struct vnode *");
SDT_PROBE_DEFINE3(vfs, namecache, zap_negative, done, "struct vnode *",
    "char *", "int");
SDT_PROBE_DEFINE3(vfs, namecache, shrink_negative, done, "struct vnode *",
    "char *", "int");

/*
 * This structure describes the elements in the cache of recent
 * names looked up by namei.
 */

struct	namecache {
	LIST_ENTRY(namecache) nc_hash;	/* hash chain */
	LIST_ENTRY(namecache) nc_src;	/* source vnode list */
	TAILQ_ENTRY(namecache) nc_dst;	/* destination vnode list */
	struct	vnode *nc_dvp;		/* vnode of parent of name */
	union {
		struct	vnode *nu_vp;	/* vnode the name refers to */
		u_int	nu_neghits;	/* negative entry hits */
	} n_un;
	u_char	nc_flag;		/* flag bits */
	u_char	nc_nlen;		/* length of name */
	char	nc_name[0];		/* segment name + nul */
};

/*
 * struct namecache_ts repeats struct namecache layout up to the
 * nc_nlen member.
 * struct namecache_ts is used in place of struct namecache when time(s) need
 * to be stored.  The nc_dotdottime field is used when a cache entry is mapping
 * both a non-dotdot directory name plus dotdot for the directory's
 * parent.
 */
struct	namecache_ts {
	struct	timespec nc_time;	/* timespec provided by fs */
	struct	timespec nc_dotdottime;	/* dotdot timespec provided by fs */
	int	nc_ticks;		/* ticks value when entry was added */
	struct namecache nc_nc;
};

#define	nc_vp		n_un.nu_vp
#define	nc_neghits	n_un.nu_neghits

/*
 * Flags in namecache.nc_flag
 */
#define NCF_WHITE	0x01
#define NCF_ISDOTDOT	0x02
#define	NCF_TS		0x04
#define	NCF_DTS		0x08
#define	NCF_DVDROP	0x10
#define	NCF_NEGATIVE	0x20
#define	NCF_HOTNEGATIVE	0x40

/*
 * Name caching works as follows:
 *
 * Names found by directory scans are retained in a cache
 * for future reference.  It is managed LRU, so frequently
 * used names will hang around.  Cache is indexed by hash value
 * obtained from (vp, name) where vp refers to the directory
 * containing name.
 *
 * If it is a "negative" entry, (i.e. for a name that is known NOT to
 * exist) the vnode pointer will be NULL.
 *
 * Upon reaching the last segment of a path, if the reference
 * is for DELETE, or NOCACHE is set (rewrite), and the
 * name is located in the cache, it will be dropped.
 *
 * These locks are used (in the order in which they can be taken):
 * NAME		TYPE	ROLE
 * vnodelock	mtx	vnode lists and v_cache_dd field protection
 * bucketlock	rwlock	for access to given set of hash buckets
 * neglist	mtx	negative entry LRU management
 *
 * Additionally, ncneg_shrink_lock mtx is used to have at most one thread
 * shrinking the LRU list.
 *
 * It is legal to take multiple vnodelock and bucketlock locks. The locking
 * order is lower address first. Both are recursive.
 *
 * "." lookups are lockless.
 *
 * ".." and vnode -> name lookups require vnodelock.
 *
 * name -> vnode lookup requires the relevant bucketlock to be held for reading.
 *
 * Insertions and removals of entries require involved vnodes and bucketlocks
 * to be write-locked to prevent other threads from seeing the entry.
 *
 * Some lookups result in removal of the found entry (e.g. getting rid of a
 * negative entry with the intent to create a positive one), which poses a
 * problem when multiple threads reach the state. Similarly, two different
 * threads can purge two different vnodes and try to remove the same name.
 *
 * If the already held vnode lock is lower than the second required lock, we
 * can just take the other lock. However, in the opposite case, this could
 * deadlock. As such, this is resolved by trylocking and if that fails unlocking
 * the first node, locking everything in order and revalidating the state.
 */

/*
 * Structures associated with name caching.
 */
#define NCHHASH(hash) \
	(&nchashtbl[(hash) & nchash])
static __read_mostly LIST_HEAD(nchashhead, namecache) *nchashtbl;/* Hash Table */
static u_long __read_mostly	nchash;			/* size of hash table */
SYSCTL_ULONG(_debug, OID_AUTO, nchash, CTLFLAG_RD, &nchash, 0,
    "Size of namecache hash table");
static u_long __read_mostly	ncnegfactor = 12; /* ratio of negative entries */
SYSCTL_ULONG(_vfs, OID_AUTO, ncnegfactor, CTLFLAG_RW, &ncnegfactor, 0,
    "Ratio of negative namecache entries");
static u_long __exclusive_cache_line	numneg;	/* number of negative entries allocated */
SYSCTL_ULONG(_debug, OID_AUTO, numneg, CTLFLAG_RD, &numneg, 0,
    "Number of negative entries in namecache");
static u_long __exclusive_cache_line	numcache;/* number of cache entries allocated */
SYSCTL_ULONG(_debug, OID_AUTO, numcache, CTLFLAG_RD, &numcache, 0,
    "Number of namecache entries");
static u_long __exclusive_cache_line	numcachehv;/* number of cache entries with vnodes held */
SYSCTL_ULONG(_debug, OID_AUTO, numcachehv, CTLFLAG_RD, &numcachehv, 0,
    "Number of namecache entries with vnodes held");
u_int __read_mostly	ncsizefactor = 2;
SYSCTL_UINT(_vfs, OID_AUTO, ncsizefactor, CTLFLAG_RW, &ncsizefactor, 0,
    "Size factor for namecache");
static u_int __read_mostly	ncpurgeminvnodes;
SYSCTL_UINT(_vfs, OID_AUTO, ncpurgeminvnodes, CTLFLAG_RW, &ncpurgeminvnodes, 0,
    "Number of vnodes below which purgevfs ignores the request");
static u_int __read_mostly	ncneghitsrequeue = 8;
SYSCTL_UINT(_vfs, OID_AUTO, ncneghitsrequeue, CTLFLAG_RW, &ncneghitsrequeue, 0,
    "Number of hits to requeue a negative entry in the LRU list");

struct nchstats	nchstats;		/* cache effectiveness statistics */

static struct mtx       ncneg_shrink_lock;
static int	shrink_list_turn;

struct neglist {
	struct mtx		nl_lock;
	TAILQ_HEAD(, namecache) nl_list;
} __aligned(CACHE_LINE_SIZE);

static struct neglist __read_mostly	*neglists;
static struct neglist ncneg_hot;

#define	numneglists (ncneghash + 1)
static u_int __read_mostly	ncneghash;
static inline struct neglist *
NCP2NEGLIST(struct namecache *ncp)
{

	return (&neglists[(((uintptr_t)(ncp) >> 8) & ncneghash)]);
}

#define	numbucketlocks (ncbuckethash + 1)
static u_int __read_mostly  ncbuckethash;
static struct rwlock_padalign __read_mostly  *bucketlocks;
#define	HASH2BUCKETLOCK(hash) \
	((struct rwlock *)(&bucketlocks[((hash) & ncbuckethash)]))

#define	numvnodelocks (ncvnodehash + 1)
static u_int __read_mostly  ncvnodehash;
static struct mtx __read_mostly *vnodelocks;
static inline struct mtx *
VP2VNODELOCK(struct vnode *vp)
{

	return (&vnodelocks[(((uintptr_t)(vp) >> 8) & ncvnodehash)]);
}

/*
 * UMA zones for the VFS cache.
 *
 * The small cache is used for entries with short names, which are the
 * most common.  The large cache is used for entries which are too big to
 * fit in the small cache.
 */
static uma_zone_t __read_mostly cache_zone_small;
static uma_zone_t __read_mostly cache_zone_small_ts;
static uma_zone_t __read_mostly cache_zone_large;
static uma_zone_t __read_mostly cache_zone_large_ts;

#define	CACHE_PATH_CUTOFF	35

static struct namecache *
cache_alloc(int len, int ts)
{
	struct namecache_ts *ncp_ts;
	struct namecache *ncp;

	if (__predict_false(ts)) {
		if (len <= CACHE_PATH_CUTOFF)
			ncp_ts = uma_zalloc(cache_zone_small_ts, M_WAITOK);
		else
			ncp_ts = uma_zalloc(cache_zone_large_ts, M_WAITOK);
		ncp = &ncp_ts->nc_nc;
	} else {
		if (len <= CACHE_PATH_CUTOFF)
			ncp = uma_zalloc(cache_zone_small, M_WAITOK);
		else
			ncp = uma_zalloc(cache_zone_large, M_WAITOK);
	}
	return (ncp);
}

static void
cache_free(struct namecache *ncp)
{
	struct namecache_ts *ncp_ts;

	if (ncp == NULL)
		return;
	if ((ncp->nc_flag & NCF_DVDROP) != 0)
		vdrop(ncp->nc_dvp);
	if (__predict_false(ncp->nc_flag & NCF_TS)) {
		ncp_ts = __containerof(ncp, struct namecache_ts, nc_nc);
		if (ncp->nc_nlen <= CACHE_PATH_CUTOFF)
			uma_zfree(cache_zone_small_ts, ncp_ts);
		else
			uma_zfree(cache_zone_large_ts, ncp_ts);
	} else {
		if (ncp->nc_nlen <= CACHE_PATH_CUTOFF)
			uma_zfree(cache_zone_small, ncp);
		else
			uma_zfree(cache_zone_large, ncp);
	}
}

static void
cache_out_ts(struct namecache *ncp, struct timespec *tsp, int *ticksp)
{
	struct namecache_ts *ncp_ts;

	KASSERT((ncp->nc_flag & NCF_TS) != 0 ||
	    (tsp == NULL && ticksp == NULL),
	    ("No NCF_TS"));

	if (tsp == NULL && ticksp == NULL)
		return;

	ncp_ts = __containerof(ncp, struct namecache_ts, nc_nc);
	if (tsp != NULL)
		*tsp = ncp_ts->nc_time;
	if (ticksp != NULL)
		*ticksp = ncp_ts->nc_ticks;
}

static int __read_mostly	doingcache = 1;	/* 1 => enable the cache */
SYSCTL_INT(_debug, OID_AUTO, vfscache, CTLFLAG_RW, &doingcache, 0,
    "VFS namecache enabled");

/* Export size information to userland */
SYSCTL_INT(_debug_sizeof, OID_AUTO, namecache, CTLFLAG_RD, SYSCTL_NULL_INT_PTR,
    sizeof(struct namecache), "sizeof(struct namecache)");

/*
 * The new name cache statistics
 */
static SYSCTL_NODE(_vfs, OID_AUTO, cache, CTLFLAG_RW, 0,
    "Name cache statistics");
#define STATNODE_ULONG(name, descr)	\
	SYSCTL_ULONG(_vfs_cache, OID_AUTO, name, CTLFLAG_RD, &name, 0, descr);
#define STATNODE_COUNTER(name, descr)	\
	static counter_u64_t __read_mostly name; \
	SYSCTL_COUNTER_U64(_vfs_cache, OID_AUTO, name, CTLFLAG_RD, &name, descr);
STATNODE_ULONG(numneg, "Number of negative cache entries");
STATNODE_ULONG(numcache, "Number of cache entries");
STATNODE_COUNTER(numcalls, "Number of cache lookups");
STATNODE_COUNTER(dothits, "Number of '.' hits");
STATNODE_COUNTER(dotdothits, "Number of '..' hits");
STATNODE_COUNTER(numchecks, "Number of checks in lookup");
STATNODE_COUNTER(nummiss, "Number of cache misses");
STATNODE_COUNTER(nummisszap, "Number of cache misses we do not want to cache");
STATNODE_COUNTER(numposzaps,
    "Number of cache hits (positive) we do not want to cache");
STATNODE_COUNTER(numposhits, "Number of cache hits (positive)");
STATNODE_COUNTER(numnegzaps,
    "Number of cache hits (negative) we do not want to cache");
STATNODE_COUNTER(numneghits, "Number of cache hits (negative)");
/* These count for kern___getcwd(), too. */
STATNODE_COUNTER(numfullpathcalls, "Number of fullpath search calls");
STATNODE_COUNTER(numfullpathfail1, "Number of fullpath search errors (ENOTDIR)");
STATNODE_COUNTER(numfullpathfail2,
    "Number of fullpath search errors (VOP_VPTOCNP failures)");
STATNODE_COUNTER(numfullpathfail4, "Number of fullpath search errors (ENOMEM)");
STATNODE_COUNTER(numfullpathfound, "Number of successful fullpath calls");
static long zap_and_exit_bucket_fail; STATNODE_ULONG(zap_and_exit_bucket_fail,
    "Number of times zap_and_exit failed to lock");
static long cache_lock_vnodes_cel_3_failures;
STATNODE_ULONG(cache_lock_vnodes_cel_3_failures,
    "Number of times 3-way vnode locking failed");

static void cache_zap_locked(struct namecache *ncp, bool neg_locked);
static int vn_fullpath1(struct thread *td, struct vnode *vp, struct vnode *rdir,
    char *buf, char **retbuf, u_int buflen);

static MALLOC_DEFINE(M_VFSCACHE, "vfscache", "VFS name cache entries");

static int cache_yield;
SYSCTL_INT(_vfs_cache, OID_AUTO, yield, CTLFLAG_RD, &cache_yield, 0,
    "Number of times cache called yield");

static void
cache_maybe_yield(void)
{

	if (should_yield()) {
		cache_yield++;
		kern_yield(PRI_USER);
	}
}

static inline void
cache_assert_vlp_locked(struct mtx *vlp)
{

	if (vlp != NULL)
		mtx_assert(vlp, MA_OWNED);
}

static inline void
cache_assert_vnode_locked(struct vnode *vp)
{
	struct mtx *vlp;

	vlp = VP2VNODELOCK(vp);
	cache_assert_vlp_locked(vlp);
}

static uint32_t
cache_get_hash(char *name, u_char len, struct vnode *dvp)
{
	uint32_t hash;

	hash = fnv_32_buf(name, len, FNV1_32_INIT);
	hash = fnv_32_buf(&dvp, sizeof(dvp), hash);
	return (hash);
}

static inline struct rwlock *
NCP2BUCKETLOCK(struct namecache *ncp)
{
	uint32_t hash;

	hash = cache_get_hash(ncp->nc_name, ncp->nc_nlen, ncp->nc_dvp);
	return (HASH2BUCKETLOCK(hash));
}

#ifdef INVARIANTS
static void
cache_assert_bucket_locked(struct namecache *ncp, int mode)
{
	struct rwlock *blp;

	blp = NCP2BUCKETLOCK(ncp);
	rw_assert(blp, mode);
}
#else
#define cache_assert_bucket_locked(x, y) do { } while (0)
#endif

#define cache_sort(x, y)	_cache_sort((void **)(x), (void **)(y))
static void
_cache_sort(void **p1, void **p2)
{
	void *tmp;

	if (*p1 > *p2) {
		tmp = *p2;
		*p2 = *p1;
		*p1 = tmp;
	}
}

static void
cache_lock_all_buckets(void)
{
	u_int i;

	for (i = 0; i < numbucketlocks; i++)
		rw_wlock(&bucketlocks[i]);
}

static void
cache_unlock_all_buckets(void)
{
	u_int i;

	for (i = 0; i < numbucketlocks; i++)
		rw_wunlock(&bucketlocks[i]);
}

static void
cache_lock_all_vnodes(void)
{
	u_int i;

	for (i = 0; i < numvnodelocks; i++)
		mtx_lock(&vnodelocks[i]);
}

static void
cache_unlock_all_vnodes(void)
{
	u_int i;

	for (i = 0; i < numvnodelocks; i++)
		mtx_unlock(&vnodelocks[i]);
}

static int
cache_trylock_vnodes(struct mtx *vlp1, struct mtx *vlp2)
{

	cache_sort(&vlp1, &vlp2);
	MPASS(vlp2 != NULL);

	if (vlp1 != NULL) {
		if (!mtx_trylock(vlp1))
			return (EAGAIN);
	}
	if (!mtx_trylock(vlp2)) {
		if (vlp1 != NULL)
			mtx_unlock(vlp1);
		return (EAGAIN);
	}

	return (0);
}

static void
cache_unlock_vnodes(struct mtx *vlp1, struct mtx *vlp2)
{

	MPASS(vlp1 != NULL || vlp2 != NULL);

	if (vlp1 != NULL)
		mtx_unlock(vlp1);
	if (vlp2 != NULL)
		mtx_unlock(vlp2);
}

static int
sysctl_nchstats(SYSCTL_HANDLER_ARGS)
{
	struct nchstats snap;

	if (req->oldptr == NULL)
		return (SYSCTL_OUT(req, 0, sizeof(snap)));

	snap = nchstats;
	snap.ncs_goodhits = counter_u64_fetch(numposhits);
	snap.ncs_neghits = counter_u64_fetch(numneghits);
	snap.ncs_badhits = counter_u64_fetch(numposzaps) +
	    counter_u64_fetch(numnegzaps);
	snap.ncs_miss = counter_u64_fetch(nummisszap) +
	    counter_u64_fetch(nummiss);

	return (SYSCTL_OUT(req, &snap, sizeof(snap)));
}
SYSCTL_PROC(_vfs_cache, OID_AUTO, nchstats, CTLTYPE_OPAQUE | CTLFLAG_RD |
    CTLFLAG_MPSAFE, 0, 0, sysctl_nchstats, "LU",
    "VFS cache effectiveness statistics");

#ifdef DIAGNOSTIC
/*
 * Grab an atomic snapshot of the name cache hash chain lengths
 */
static SYSCTL_NODE(_debug, OID_AUTO, hashstat, CTLFLAG_RW, NULL,
    "hash table stats");

static int
sysctl_debug_hashstat_rawnchash(SYSCTL_HANDLER_ARGS)
{
	struct nchashhead *ncpp;
	struct namecache *ncp;
	int i, error, n_nchash, *cntbuf;

retry:
	n_nchash = nchash + 1;	/* nchash is max index, not count */
	if (req->oldptr == NULL)
		return SYSCTL_OUT(req, 0, n_nchash * sizeof(int));
	cntbuf = malloc(n_nchash * sizeof(int), M_TEMP, M_ZERO | M_WAITOK);
	cache_lock_all_buckets();
	if (n_nchash != nchash + 1) {
		cache_unlock_all_buckets();
		free(cntbuf, M_TEMP);
		goto retry;
	}
	/* Scan hash tables counting entries */
	for (ncpp = nchashtbl, i = 0; i < n_nchash; ncpp++, i++)
		LIST_FOREACH(ncp, ncpp, nc_hash)
			cntbuf[i]++;
	cache_unlock_all_buckets();
	for (error = 0, i = 0; i < n_nchash; i++)
		if ((error = SYSCTL_OUT(req, &cntbuf[i], sizeof(int))) != 0)
			break;
	free(cntbuf, M_TEMP);
	return (error);
}
SYSCTL_PROC(_debug_hashstat, OID_AUTO, rawnchash, CTLTYPE_INT|CTLFLAG_RD|
    CTLFLAG_MPSAFE, 0, 0, sysctl_debug_hashstat_rawnchash, "S,int",
    "nchash chain lengths");

static int
sysctl_debug_hashstat_nchash(SYSCTL_HANDLER_ARGS)
{
	int error;
	struct nchashhead *ncpp;
	struct namecache *ncp;
	int n_nchash;
	int count, maxlength, used, pct;

	if (!req->oldptr)
		return SYSCTL_OUT(req, 0, 4 * sizeof(int));

	cache_lock_all_buckets();
	n_nchash = nchash + 1;	/* nchash is max index, not count */
	used = 0;
	maxlength = 0;

	/* Scan hash tables for applicable entries */
	for (ncpp = nchashtbl; n_nchash > 0; n_nchash--, ncpp++) {
		count = 0;
		LIST_FOREACH(ncp, ncpp, nc_hash) {
			count++;
		}
		if (count)
			used++;
		if (maxlength < count)
			maxlength = count;
	}
	n_nchash = nchash + 1;
	cache_unlock_all_buckets();
	pct = (used * 100) / (n_nchash / 100);
	error = SYSCTL_OUT(req, &n_nchash, sizeof(n_nchash));
	if (error)
		return (error);
	error = SYSCTL_OUT(req, &used, sizeof(used));
	if (error)
		return (error);
	error = SYSCTL_OUT(req, &maxlength, sizeof(maxlength));
	if (error)
		return (error);
	error = SYSCTL_OUT(req, &pct, sizeof(pct));
	if (error)
		return (error);
	return (0);
}
SYSCTL_PROC(_debug_hashstat, OID_AUTO, nchash, CTLTYPE_INT|CTLFLAG_RD|
    CTLFLAG_MPSAFE, 0, 0, sysctl_debug_hashstat_nchash, "I",
    "nchash statistics (number of total/used buckets, maximum chain length, usage percentage)");
#endif

/*
 * Negative entries management
 *
 * A variation of LRU scheme is used. New entries are hashed into one of
 * numneglists cold lists. Entries get promoted to the hot list on first hit.
 * Partial LRU for the hot list is maintained by requeueing them every
 * ncneghitsrequeue hits.
 *
 * The shrinker will demote hot list head and evict from the cold list in a
 * round-robin manner.
 */
static void
cache_negative_hit(struct namecache *ncp)
{
	struct neglist *neglist;
	u_int hits;

	MPASS(ncp->nc_flag & NCF_NEGATIVE);
	hits = atomic_fetchadd_int(&ncp->nc_neghits, 1);
	if (ncp->nc_flag & NCF_HOTNEGATIVE) {
		if ((hits % ncneghitsrequeue) != 0)
			return;
		mtx_lock(&ncneg_hot.nl_lock);
		if (ncp->nc_flag & NCF_HOTNEGATIVE) {
			TAILQ_REMOVE(&ncneg_hot.nl_list, ncp, nc_dst);
			TAILQ_INSERT_TAIL(&ncneg_hot.nl_list, ncp, nc_dst);
			mtx_unlock(&ncneg_hot.nl_lock);
			return;
		}
		/*
		 * The shrinker cleared the flag and removed the entry from
		 * the hot list. Put it back.
		 */
	} else {
		mtx_lock(&ncneg_hot.nl_lock);
	}
	neglist = NCP2NEGLIST(ncp);
	mtx_lock(&neglist->nl_lock);
	if (!(ncp->nc_flag & NCF_HOTNEGATIVE)) {
		TAILQ_REMOVE(&neglist->nl_list, ncp, nc_dst);
		TAILQ_INSERT_TAIL(&ncneg_hot.nl_list, ncp, nc_dst);
		ncp->nc_flag |= NCF_HOTNEGATIVE;
	}
	mtx_unlock(&neglist->nl_lock);
	mtx_unlock(&ncneg_hot.nl_lock);
}

static void
cache_negative_insert(struct namecache *ncp, bool neg_locked)
{
	struct neglist *neglist;

	MPASS(ncp->nc_flag & NCF_NEGATIVE);
	cache_assert_bucket_locked(ncp, RA_WLOCKED);
	neglist = NCP2NEGLIST(ncp);
	if (!neg_locked) {
		mtx_lock(&neglist->nl_lock);
	} else {
		mtx_assert(&neglist->nl_lock, MA_OWNED);
	}
	TAILQ_INSERT_TAIL(&neglist->nl_list, ncp, nc_dst);
	if (!neg_locked)
		mtx_unlock(&neglist->nl_lock);
	atomic_add_rel_long(&numneg, 1);
}

static void
cache_negative_remove(struct namecache *ncp, bool neg_locked)
{
	struct neglist *neglist;
	bool hot_locked = false;
	bool list_locked = false;

	MPASS(ncp->nc_flag & NCF_NEGATIVE);
	cache_assert_bucket_locked(ncp, RA_WLOCKED);
	neglist = NCP2NEGLIST(ncp);
	if (!neg_locked) {
		if (ncp->nc_flag & NCF_HOTNEGATIVE) {
			hot_locked = true;
			mtx_lock(&ncneg_hot.nl_lock);
			if (!(ncp->nc_flag & NCF_HOTNEGATIVE)) {
				list_locked = true;
				mtx_lock(&neglist->nl_lock);
			}
		} else {
			list_locked = true;
			mtx_lock(&neglist->nl_lock);
		}
	}
	if (ncp->nc_flag & NCF_HOTNEGATIVE) {
		mtx_assert(&ncneg_hot.nl_lock, MA_OWNED);
		TAILQ_REMOVE(&ncneg_hot.nl_list, ncp, nc_dst);
	} else {
		mtx_assert(&neglist->nl_lock, MA_OWNED);
		TAILQ_REMOVE(&neglist->nl_list, ncp, nc_dst);
	}
	if (list_locked)
		mtx_unlock(&neglist->nl_lock);
	if (hot_locked)
		mtx_unlock(&ncneg_hot.nl_lock);
	atomic_subtract_rel_long(&numneg, 1);
}

static void
cache_negative_shrink_select(int start, struct namecache **ncpp,
    struct neglist **neglistpp)
{
	struct neglist *neglist;
	struct namecache *ncp;
	int i;

	*ncpp = ncp = NULL;
	neglist = NULL;

	for (i = start; i < numneglists; i++) {
		neglist = &neglists[i];
		if (TAILQ_FIRST(&neglist->nl_list) == NULL)
			continue;
		mtx_lock(&neglist->nl_lock);
		ncp = TAILQ_FIRST(&neglist->nl_list);
		if (ncp != NULL)
			break;
		mtx_unlock(&neglist->nl_lock);
	}

	*neglistpp = neglist;
	*ncpp = ncp;
}

static void
cache_negative_zap_one(void)
{
	struct namecache *ncp, *ncp2;
	struct neglist *neglist;
	struct mtx *dvlp;
	struct rwlock *blp;

	if (!mtx_trylock(&ncneg_shrink_lock))
		return;

	mtx_lock(&ncneg_hot.nl_lock);
	ncp = TAILQ_FIRST(&ncneg_hot.nl_list);
	if (ncp != NULL) {
		neglist = NCP2NEGLIST(ncp);
		mtx_lock(&neglist->nl_lock);
		TAILQ_REMOVE(&ncneg_hot.nl_list, ncp, nc_dst);
		TAILQ_INSERT_TAIL(&neglist->nl_list, ncp, nc_dst);
		ncp->nc_flag &= ~NCF_HOTNEGATIVE;
		mtx_unlock(&neglist->nl_lock);
	}

	cache_negative_shrink_select(shrink_list_turn, &ncp, &neglist);
	shrink_list_turn++;
	if (shrink_list_turn == numneglists)
		shrink_list_turn = 0;
	if (ncp == NULL && shrink_list_turn == 0)
		cache_negative_shrink_select(shrink_list_turn, &ncp, &neglist);
	if (ncp == NULL) {
		mtx_unlock(&ncneg_hot.nl_lock);
		goto out;
	}

	MPASS(ncp->nc_flag & NCF_NEGATIVE);
	dvlp = VP2VNODELOCK(ncp->nc_dvp);
	blp = NCP2BUCKETLOCK(ncp);
	mtx_unlock(&neglist->nl_lock);
	mtx_unlock(&ncneg_hot.nl_lock);
	mtx_lock(dvlp);
	rw_wlock(blp);
	mtx_lock(&neglist->nl_lock);
	ncp2 = TAILQ_FIRST(&neglist->nl_list);
	if (ncp != ncp2 || dvlp != VP2VNODELOCK(ncp2->nc_dvp) ||
	    blp != NCP2BUCKETLOCK(ncp2) || !(ncp2->nc_flag & NCF_NEGATIVE)) {
		ncp = NULL;
		goto out_unlock_all;
	}
	SDT_PROBE3(vfs, namecache, shrink_negative, done, ncp->nc_dvp,
	    ncp->nc_name, ncp->nc_neghits);

	cache_zap_locked(ncp, true);
out_unlock_all:
	mtx_unlock(&neglist->nl_lock);
	rw_wunlock(blp);
	mtx_unlock(dvlp);
out:
	mtx_unlock(&ncneg_shrink_lock);
	cache_free(ncp);
}

/*
 * cache_zap_locked():
 *
 *   Removes a namecache entry from cache, whether it contains an actual
 *   pointer to a vnode or if it is just a negative cache entry.
 */
static void
cache_zap_locked(struct namecache *ncp, bool neg_locked)
{

	if (!(ncp->nc_flag & NCF_NEGATIVE))
		cache_assert_vnode_locked(ncp->nc_vp);
	cache_assert_vnode_locked(ncp->nc_dvp);
	cache_assert_bucket_locked(ncp, RA_WLOCKED);

	CTR2(KTR_VFS, "cache_zap(%p) vp %p", ncp,
	    (ncp->nc_flag & NCF_NEGATIVE) ? NULL : ncp->nc_vp);
	if (!(ncp->nc_flag & NCF_NEGATIVE)) {
		SDT_PROBE3(vfs, namecache, zap, done, ncp->nc_dvp,
		    ncp->nc_name, ncp->nc_vp);
	} else {
		SDT_PROBE3(vfs, namecache, zap_negative, done, ncp->nc_dvp,
		    ncp->nc_name, ncp->nc_neghits);
	}
	LIST_REMOVE(ncp, nc_hash);
	if (!(ncp->nc_flag & NCF_NEGATIVE)) {
		TAILQ_REMOVE(&ncp->nc_vp->v_cache_dst, ncp, nc_dst);
		if (ncp == ncp->nc_vp->v_cache_dd)
			ncp->nc_vp->v_cache_dd = NULL;
	} else {
		cache_negative_remove(ncp, neg_locked);
	}
	if (ncp->nc_flag & NCF_ISDOTDOT) {
		if (ncp == ncp->nc_dvp->v_cache_dd)
			ncp->nc_dvp->v_cache_dd = NULL;
	} else {
		LIST_REMOVE(ncp, nc_src);
		if (LIST_EMPTY(&ncp->nc_dvp->v_cache_src)) {
			ncp->nc_flag |= NCF_DVDROP;
			atomic_subtract_rel_long(&numcachehv, 1);
		}
	}
	atomic_subtract_rel_long(&numcache, 1);
}

static void
cache_zap_negative_locked_vnode_kl(struct namecache *ncp, struct vnode *vp)
{
	struct rwlock *blp;

	MPASS(ncp->nc_dvp == vp);
	MPASS(ncp->nc_flag & NCF_NEGATIVE);
	cache_assert_vnode_locked(vp);

	blp = NCP2BUCKETLOCK(ncp);
	rw_wlock(blp);
	cache_zap_locked(ncp, false);
	rw_wunlock(blp);
}

static bool
cache_zap_locked_vnode_kl2(struct namecache *ncp, struct vnode *vp,
    struct mtx **vlpp)
{
	struct mtx *pvlp, *vlp1, *vlp2, *to_unlock;
	struct rwlock *blp;

	MPASS(vp == ncp->nc_dvp || vp == ncp->nc_vp);
	cache_assert_vnode_locked(vp);

	if (ncp->nc_flag & NCF_NEGATIVE) {
		if (*vlpp != NULL) {
			mtx_unlock(*vlpp);
			*vlpp = NULL;
		}
		cache_zap_negative_locked_vnode_kl(ncp, vp);
		return (true);
	}

	pvlp = VP2VNODELOCK(vp);
	blp = NCP2BUCKETLOCK(ncp);
	vlp1 = VP2VNODELOCK(ncp->nc_dvp);
	vlp2 = VP2VNODELOCK(ncp->nc_vp);

	if (*vlpp == vlp1 || *vlpp == vlp2) {
		to_unlock = *vlpp;
		*vlpp = NULL;
	} else {
		if (*vlpp != NULL) {
			mtx_unlock(*vlpp);
			*vlpp = NULL;
		}
		cache_sort(&vlp1, &vlp2);
		if (vlp1 == pvlp) {
			mtx_lock(vlp2);
			to_unlock = vlp2;
		} else {
			if (!mtx_trylock(vlp1))
				goto out_relock;
			to_unlock = vlp1;
		}
	}
	rw_wlock(blp);
	cache_zap_locked(ncp, false);
	rw_wunlock(blp);
	if (to_unlock != NULL)
		mtx_unlock(to_unlock);
	return (true);

out_relock:
	mtx_unlock(vlp2);
	mtx_lock(vlp1);
	mtx_lock(vlp2);
	MPASS(*vlpp == NULL);
	*vlpp = vlp1;
	return (false);
}

static int
cache_zap_locked_vnode(struct namecache *ncp, struct vnode *vp)
{
	struct mtx *pvlp, *vlp1, *vlp2, *to_unlock;
	struct rwlock *blp;
	int error = 0;

	MPASS(vp == ncp->nc_dvp || vp == ncp->nc_vp);
	cache_assert_vnode_locked(vp);

	pvlp = VP2VNODELOCK(vp);
	if (ncp->nc_flag & NCF_NEGATIVE) {
		cache_zap_negative_locked_vnode_kl(ncp, vp);
		goto out;
	}

	blp = NCP2BUCKETLOCK(ncp);
	vlp1 = VP2VNODELOCK(ncp->nc_dvp);
	vlp2 = VP2VNODELOCK(ncp->nc_vp);
	cache_sort(&vlp1, &vlp2);
	if (vlp1 == pvlp) {
		mtx_lock(vlp2);
		to_unlock = vlp2;
	} else {
		if (!mtx_trylock(vlp1)) {
			error = EAGAIN;
			goto out;
		}
		to_unlock = vlp1;
	}
	rw_wlock(blp);
	cache_zap_locked(ncp, false);
	rw_wunlock(blp);
	mtx_unlock(to_unlock);
out:
	mtx_unlock(pvlp);
	return (error);
}

static int
cache_zap_wlocked_bucket(struct namecache *ncp, struct rwlock *blp)
{
	struct mtx *dvlp, *vlp;

	cache_assert_bucket_locked(ncp, RA_WLOCKED);

	dvlp = VP2VNODELOCK(ncp->nc_dvp);
	vlp = NULL;
	if (!(ncp->nc_flag & NCF_NEGATIVE))
		vlp = VP2VNODELOCK(ncp->nc_vp);
	if (cache_trylock_vnodes(dvlp, vlp) == 0) {
		cache_zap_locked(ncp, false);
		rw_wunlock(blp);
		cache_unlock_vnodes(dvlp, vlp);
		return (0);
	}

	rw_wunlock(blp);
	return (EAGAIN);
}

static int
cache_zap_rlocked_bucket(struct namecache *ncp, struct rwlock *blp)
{
	struct mtx *dvlp, *vlp;

	cache_assert_bucket_locked(ncp, RA_RLOCKED);

	dvlp = VP2VNODELOCK(ncp->nc_dvp);
	vlp = NULL;
	if (!(ncp->nc_flag & NCF_NEGATIVE))
		vlp = VP2VNODELOCK(ncp->nc_vp);
	if (cache_trylock_vnodes(dvlp, vlp) == 0) {
		rw_runlock(blp);
		rw_wlock(blp);
		cache_zap_locked(ncp, false);
		rw_wunlock(blp);
		cache_unlock_vnodes(dvlp, vlp);
		return (0);
	}

	rw_runlock(blp);
	return (EAGAIN);
}

static int
cache_zap_wlocked_bucket_kl(struct namecache *ncp, struct rwlock *blp,
    struct mtx **vlpp1, struct mtx **vlpp2)
{
	struct mtx *dvlp, *vlp;

	cache_assert_bucket_locked(ncp, RA_WLOCKED);

	dvlp = VP2VNODELOCK(ncp->nc_dvp);
	vlp = NULL;
	if (!(ncp->nc_flag & NCF_NEGATIVE))
		vlp = VP2VNODELOCK(ncp->nc_vp);
	cache_sort(&dvlp, &vlp);

	if (*vlpp1 == dvlp && *vlpp2 == vlp) {
		cache_zap_locked(ncp, false);
		cache_unlock_vnodes(dvlp, vlp);
		*vlpp1 = NULL;
		*vlpp2 = NULL;
		return (0);
	}

	if (*vlpp1 != NULL)
		mtx_unlock(*vlpp1);
	if (*vlpp2 != NULL)
		mtx_unlock(*vlpp2);
	*vlpp1 = NULL;
	*vlpp2 = NULL;

	if (cache_trylock_vnodes(dvlp, vlp) == 0) {
		cache_zap_locked(ncp, false);
		cache_unlock_vnodes(dvlp, vlp);
		return (0);
	}

	rw_wunlock(blp);
	*vlpp1 = dvlp;
	*vlpp2 = vlp;
	if (*vlpp1 != NULL)
		mtx_lock(*vlpp1);
	mtx_lock(*vlpp2);
	rw_wlock(blp);
	return (EAGAIN);
}

static void
cache_lookup_unlock(struct rwlock *blp, struct mtx *vlp)
{

	if (blp != NULL) {
		rw_runlock(blp);
	} else {
		mtx_unlock(vlp);
	}
}

static int __noinline
cache_lookup_dot(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp,
    struct timespec *tsp, int *ticksp)
{
	int ltype;

	*vpp = dvp;
	CTR2(KTR_VFS, "cache_lookup(%p, %s) found via .",
			dvp, cnp->cn_nameptr);
	counter_u64_add(dothits, 1);
	SDT_PROBE3(vfs, namecache, lookup, hit, dvp, ".", *vpp);
	if (tsp != NULL)
		timespecclear(tsp);
	if (ticksp != NULL)
		*ticksp = ticks;
	vrefact(*vpp);
	/*
	 * When we lookup "." we still can be asked to lock it
	 * differently...
	 */
	ltype = cnp->cn_lkflags & LK_TYPE_MASK;
	if (ltype != VOP_ISLOCKED(*vpp)) {
		if (ltype == LK_EXCLUSIVE) {
			vn_lock(*vpp, LK_UPGRADE | LK_RETRY);
			if ((*vpp)->v_iflag & VI_DOOMED) {
				/* forced unmount */
				vrele(*vpp);
				*vpp = NULL;
				return (ENOENT);
			}
		} else
			vn_lock(*vpp, LK_DOWNGRADE | LK_RETRY);
	}
	return (-1);
}

/*
 * Lookup an entry in the cache
 *
 * Lookup is called with dvp pointing to the directory to search,
 * cnp pointing to the name of the entry being sought. If the lookup
 * succeeds, the vnode is returned in *vpp, and a status of -1 is
 * returned. If the lookup determines that the name does not exist
 * (negative caching), a status of ENOENT is returned. If the lookup
 * fails, a status of zero is returned.  If the directory vnode is
 * recycled out from under us due to a forced unmount, a status of
 * ENOENT is returned.
 *
 * vpp is locked and ref'd on return.  If we're looking up DOTDOT, dvp is
 * unlocked.  If we're looking up . an extra ref is taken, but the lock is
 * not recursively acquired.
 */

static __noinline int
cache_lookup_nomakeentry(struct vnode *dvp, struct vnode **vpp,
    struct componentname *cnp, struct timespec *tsp, int *ticksp)
{
	struct namecache *ncp;
	struct rwlock *blp;
	struct mtx *dvlp, *dvlp2;
	uint32_t hash;
	int error;

	if (cnp->cn_namelen == 2 &&
	    cnp->cn_nameptr[0] == '.' && cnp->cn_nameptr[1] == '.') {
		counter_u64_add(dotdothits, 1);
		dvlp = VP2VNODELOCK(dvp);
		dvlp2 = NULL;
		mtx_lock(dvlp);
retry_dotdot:
		ncp = dvp->v_cache_dd;
		if (ncp == NULL) {
			SDT_PROBE3(vfs, namecache, lookup, miss, dvp,
			    "..", NULL);
			mtx_unlock(dvlp);
			if (dvlp2 != NULL)
				mtx_unlock(dvlp2);
			return (0);
		}
		if ((ncp->nc_flag & NCF_ISDOTDOT) != 0) {
			if (ncp->nc_dvp != dvp)
				panic("dvp %p v_cache_dd %p\n", dvp, ncp);
			if (!cache_zap_locked_vnode_kl2(ncp,
			    dvp, &dvlp2))
				goto retry_dotdot;
			MPASS(dvp->v_cache_dd == NULL);
			mtx_unlock(dvlp);
			if (dvlp2 != NULL)
				mtx_unlock(dvlp2);
			cache_free(ncp);
		} else {
			dvp->v_cache_dd = NULL;
			mtx_unlock(dvlp);
			if (dvlp2 != NULL)
				mtx_unlock(dvlp2);
		}
		return (0);
	}

	hash = cache_get_hash(cnp->cn_nameptr, cnp->cn_namelen, dvp);
	blp = HASH2BUCKETLOCK(hash);
retry:
	if (LIST_EMPTY(NCHHASH(hash)))
		goto out_no_entry;

	rw_wlock(blp);

	LIST_FOREACH(ncp, (NCHHASH(hash)), nc_hash) {
		counter_u64_add(numchecks, 1);
		if (ncp->nc_dvp == dvp && ncp->nc_nlen == cnp->cn_namelen &&
		    !bcmp(ncp->nc_name, cnp->cn_nameptr, ncp->nc_nlen))
			break;
	}

	/* We failed to find an entry */
	if (ncp == NULL) {
		rw_wunlock(blp);
		goto out_no_entry;
	}

	counter_u64_add(numposzaps, 1);

	error = cache_zap_wlocked_bucket(ncp, blp);
	if (error != 0) {
		zap_and_exit_bucket_fail++;
		cache_maybe_yield();
		goto retry;
	}
	cache_free(ncp);
	return (0);
out_no_entry:
	SDT_PROBE3(vfs, namecache, lookup, miss, dvp, cnp->cn_nameptr, NULL);
	counter_u64_add(nummisszap, 1);
	return (0);
}

int
cache_lookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp,
    struct timespec *tsp, int *ticksp)
{
	struct namecache_ts *ncp_ts;
	struct namecache *ncp;
	struct rwlock *blp;
	struct mtx *dvlp;
	uint32_t hash;
	int error, ltype;

	if (__predict_false(!doingcache)) {
		cnp->cn_flags &= ~MAKEENTRY;
		return (0);
	}

	counter_u64_add(numcalls, 1);

	if (__predict_false(cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.'))
		return (cache_lookup_dot(dvp, vpp, cnp, tsp, ticksp));

	if ((cnp->cn_flags & MAKEENTRY) == 0)
		return (cache_lookup_nomakeentry(dvp, vpp, cnp, tsp, ticksp));

retry:
	blp = NULL;
	dvlp = NULL;
	error = 0;
	if (cnp->cn_namelen == 2 &&
	    cnp->cn_nameptr[0] == '.' && cnp->cn_nameptr[1] == '.') {
		counter_u64_add(dotdothits, 1);
		dvlp = VP2VNODELOCK(dvp);
		mtx_lock(dvlp);
		ncp = dvp->v_cache_dd;
		if (ncp == NULL) {
			SDT_PROBE3(vfs, namecache, lookup, miss, dvp,
			    "..", NULL);
			mtx_unlock(dvlp);
			return (0);
		}
		if ((ncp->nc_flag & NCF_ISDOTDOT) != 0) {
			if (ncp->nc_flag & NCF_NEGATIVE)
				*vpp = NULL;
			else
				*vpp = ncp->nc_vp;
		} else
			*vpp = ncp->nc_dvp;
		/* Return failure if negative entry was found. */
		if (*vpp == NULL)
			goto negative_success;
		CTR3(KTR_VFS, "cache_lookup(%p, %s) found %p via ..",
		    dvp, cnp->cn_nameptr, *vpp);
		SDT_PROBE3(vfs, namecache, lookup, hit, dvp, "..",
		    *vpp);
		cache_out_ts(ncp, tsp, ticksp);
		if ((ncp->nc_flag & (NCF_ISDOTDOT | NCF_DTS)) ==
		    NCF_DTS && tsp != NULL) {
			ncp_ts = __containerof(ncp, struct namecache_ts, nc_nc);
			*tsp = ncp_ts->nc_dotdottime;
		}
		goto success;
	}

	hash = cache_get_hash(cnp->cn_nameptr, cnp->cn_namelen, dvp);
	blp = HASH2BUCKETLOCK(hash);
	rw_rlock(blp);

	LIST_FOREACH(ncp, (NCHHASH(hash)), nc_hash) {
		counter_u64_add(numchecks, 1);
		if (ncp->nc_dvp == dvp && ncp->nc_nlen == cnp->cn_namelen &&
		    !bcmp(ncp->nc_name, cnp->cn_nameptr, ncp->nc_nlen))
			break;
	}

	/* We failed to find an entry */
	if (ncp == NULL) {
		rw_runlock(blp);
		SDT_PROBE3(vfs, namecache, lookup, miss, dvp, cnp->cn_nameptr,
		    NULL);
		counter_u64_add(nummiss, 1);
		return (0);
	}

	/* We found a "positive" match, return the vnode */
	if (!(ncp->nc_flag & NCF_NEGATIVE)) {
		counter_u64_add(numposhits, 1);
		*vpp = ncp->nc_vp;
		CTR4(KTR_VFS, "cache_lookup(%p, %s) found %p via ncp %p",
		    dvp, cnp->cn_nameptr, *vpp, ncp);
		SDT_PROBE3(vfs, namecache, lookup, hit, dvp, ncp->nc_name,
		    *vpp);
		cache_out_ts(ncp, tsp, ticksp);
		goto success;
	}

negative_success:
	/* We found a negative match, and want to create it, so purge */
	if (cnp->cn_nameiop == CREATE) {
		counter_u64_add(numnegzaps, 1);
		goto zap_and_exit;
	}

	counter_u64_add(numneghits, 1);
	cache_negative_hit(ncp);
	if (ncp->nc_flag & NCF_WHITE)
		cnp->cn_flags |= ISWHITEOUT;
	SDT_PROBE2(vfs, namecache, lookup, hit__negative, dvp,
	    ncp->nc_name);
	cache_out_ts(ncp, tsp, ticksp);
	cache_lookup_unlock(blp, dvlp);
	return (ENOENT);

success:
	/*
	 * On success we return a locked and ref'd vnode as per the lookup
	 * protocol.
	 */
	MPASS(dvp != *vpp);
	ltype = 0;	/* silence gcc warning */
	if (cnp->cn_flags & ISDOTDOT) {
		ltype = VOP_ISLOCKED(dvp);
		VOP_UNLOCK(dvp, 0);
	}
	vhold(*vpp);
	cache_lookup_unlock(blp, dvlp);
	error = vget(*vpp, cnp->cn_lkflags | LK_VNHELD, cnp->cn_thread);
	if (cnp->cn_flags & ISDOTDOT) {
		vn_lock(dvp, ltype | LK_RETRY);
		if (dvp->v_iflag & VI_DOOMED) {
			if (error == 0)
				vput(*vpp);
			*vpp = NULL;
			return (ENOENT);
		}
	}
	if (error) {
		*vpp = NULL;
		goto retry;
	}
	if ((cnp->cn_flags & ISLASTCN) &&
	    (cnp->cn_lkflags & LK_TYPE_MASK) == LK_EXCLUSIVE) {
		ASSERT_VOP_ELOCKED(*vpp, "cache_lookup");
	}
	return (-1);

zap_and_exit:
	if (blp != NULL)
		error = cache_zap_rlocked_bucket(ncp, blp);
	else
		error = cache_zap_locked_vnode(ncp, dvp);
	if (error != 0) {
		zap_and_exit_bucket_fail++;
		cache_maybe_yield();
		goto retry;
	}
	cache_free(ncp);
	return (0);
}

struct celockstate {
	struct mtx *vlp[3];
	struct rwlock *blp[2];
};
CTASSERT((nitems(((struct celockstate *)0)->vlp) == 3));
CTASSERT((nitems(((struct celockstate *)0)->blp) == 2));

static inline void
cache_celockstate_init(struct celockstate *cel)
{

	bzero(cel, sizeof(*cel));
}

static void
cache_lock_vnodes_cel(struct celockstate *cel, struct vnode *vp,
    struct vnode *dvp)
{
	struct mtx *vlp1, *vlp2;

	MPASS(cel->vlp[0] == NULL);
	MPASS(cel->vlp[1] == NULL);
	MPASS(cel->vlp[2] == NULL);

	MPASS(vp != NULL || dvp != NULL);

	vlp1 = VP2VNODELOCK(vp);
	vlp2 = VP2VNODELOCK(dvp);
	cache_sort(&vlp1, &vlp2);

	if (vlp1 != NULL) {
		mtx_lock(vlp1);
		cel->vlp[0] = vlp1;
	}
	mtx_lock(vlp2);
	cel->vlp[1] = vlp2;
}

static void
cache_unlock_vnodes_cel(struct celockstate *cel)
{

	MPASS(cel->vlp[0] != NULL || cel->vlp[1] != NULL);

	if (cel->vlp[0] != NULL)
		mtx_unlock(cel->vlp[0]);
	if (cel->vlp[1] != NULL)
		mtx_unlock(cel->vlp[1]);
	if (cel->vlp[2] != NULL)
		mtx_unlock(cel->vlp[2]);
}

static bool
cache_lock_vnodes_cel_3(struct celockstate *cel, struct vnode *vp)
{
	struct mtx *vlp;
	bool ret;

	cache_assert_vlp_locked(cel->vlp[0]);
	cache_assert_vlp_locked(cel->vlp[1]);
	MPASS(cel->vlp[2] == NULL);

	MPASS(vp != NULL);
	vlp = VP2VNODELOCK(vp);

	ret = true;
	if (vlp >= cel->vlp[1]) {
		mtx_lock(vlp);
	} else {
		if (mtx_trylock(vlp))
			goto out;
		cache_lock_vnodes_cel_3_failures++;
		cache_unlock_vnodes_cel(cel);
		if (vlp < cel->vlp[0]) {
			mtx_lock(vlp);
			mtx_lock(cel->vlp[0]);
			mtx_lock(cel->vlp[1]);
		} else {
			if (cel->vlp[0] != NULL)
				mtx_lock(cel->vlp[0]);
			mtx_lock(vlp);
			mtx_lock(cel->vlp[1]);
		}
		ret = false;
	}
out:
	cel->vlp[2] = vlp;
	return (ret);
}

static void
cache_lock_buckets_cel(struct celockstate *cel, struct rwlock *blp1,
    struct rwlock *blp2)
{

	MPASS(cel->blp[0] == NULL);
	MPASS(cel->blp[1] == NULL);

	cache_sort(&blp1, &blp2);

	if (blp1 != NULL) {
		rw_wlock(blp1);
		cel->blp[0] = blp1;
	}
	rw_wlock(blp2);
	cel->blp[1] = blp2;
}

static void
cache_unlock_buckets_cel(struct celockstate *cel)
{

	if (cel->blp[0] != NULL)
		rw_wunlock(cel->blp[0]);
	rw_wunlock(cel->blp[1]);
}

/*
 * Lock part of the cache affected by the insertion.
 *
 * This means vnodelocks for dvp, vp and the relevant bucketlock.
 * However, insertion can result in removal of an old entry. In this
 * case we have an additional vnode and bucketlock pair to lock. If the
 * entry is negative, ncelock is locked instead of the vnode.
 *
 * That is, in the worst case we have to lock 3 vnodes and 2 bucketlocks, while
 * preserving the locking order (smaller address first).
 */
static void
cache_enter_lock(struct celockstate *cel, struct vnode *dvp, struct vnode *vp,
    uint32_t hash)
{
	struct namecache *ncp;
	struct rwlock *blps[2];

	blps[0] = HASH2BUCKETLOCK(hash);
	for (;;) {
		blps[1] = NULL;
		cache_lock_vnodes_cel(cel, dvp, vp);
		if (vp == NULL || vp->v_type != VDIR)
			break;
		ncp = vp->v_cache_dd;
		if (ncp == NULL)
			break;
		if ((ncp->nc_flag & NCF_ISDOTDOT) == 0)
			break;
		MPASS(ncp->nc_dvp == vp);
		blps[1] = NCP2BUCKETLOCK(ncp);
		if (ncp->nc_flag & NCF_NEGATIVE)
			break;
		if (cache_lock_vnodes_cel_3(cel, ncp->nc_vp))
			break;
		/*
		 * All vnodes got re-locked. Re-validate the state and if
		 * nothing changed we are done. Otherwise restart.
		 */
		if (ncp == vp->v_cache_dd &&
		    (ncp->nc_flag & NCF_ISDOTDOT) != 0 &&
		    blps[1] == NCP2BUCKETLOCK(ncp) &&
		    VP2VNODELOCK(ncp->nc_vp) == cel->vlp[2])
			break;
		cache_unlock_vnodes_cel(cel);
		cel->vlp[0] = NULL;
		cel->vlp[1] = NULL;
		cel->vlp[2] = NULL;
	}
	cache_lock_buckets_cel(cel, blps[0], blps[1]);
}

static void
cache_enter_lock_dd(struct celockstate *cel, struct vnode *dvp, struct vnode *vp,
    uint32_t hash)
{
	struct namecache *ncp;
	struct rwlock *blps[2];

	blps[0] = HASH2BUCKETLOCK(hash);
	for (;;) {
		blps[1] = NULL;
		cache_lock_vnodes_cel(cel, dvp, vp);
		ncp = dvp->v_cache_dd;
		if (ncp == NULL)
			break;
		if ((ncp->nc_flag & NCF_ISDOTDOT) == 0)
			break;
		MPASS(ncp->nc_dvp == dvp);
		blps[1] = NCP2BUCKETLOCK(ncp);
		if (ncp->nc_flag & NCF_NEGATIVE)
			break;
		if (cache_lock_vnodes_cel_3(cel, ncp->nc_vp))
			break;
		if (ncp == dvp->v_cache_dd &&
		    (ncp->nc_flag & NCF_ISDOTDOT) != 0 &&
		    blps[1] == NCP2BUCKETLOCK(ncp) &&
		    VP2VNODELOCK(ncp->nc_vp) == cel->vlp[2])
			break;
		cache_unlock_vnodes_cel(cel);
		cel->vlp[0] = NULL;
		cel->vlp[1] = NULL;
		cel->vlp[2] = NULL;
	}
	cache_lock_buckets_cel(cel, blps[0], blps[1]);
}

static void
cache_enter_unlock(struct celockstate *cel)
{

	cache_unlock_buckets_cel(cel);
	cache_unlock_vnodes_cel(cel);
}

/*
 * Add an entry to the cache.
 */
void
cache_enter_time(struct vnode *dvp, struct vnode *vp, struct componentname *cnp,
    struct timespec *tsp, struct timespec *dtsp)
{
	struct celockstate cel;
	struct namecache *ncp, *n2, *ndd;
	struct namecache_ts *ncp_ts, *n2_ts;
	struct nchashhead *ncpp;
	struct neglist *neglist;
	uint32_t hash;
	int flag;
	int len;
	bool neg_locked;
	int lnumcache;

	CTR3(KTR_VFS, "cache_enter(%p, %p, %s)", dvp, vp, cnp->cn_nameptr);
	VNASSERT(vp == NULL || (vp->v_iflag & VI_DOOMED) == 0, vp,
	    ("cache_enter: Adding a doomed vnode"));
	VNASSERT(dvp == NULL || (dvp->v_iflag & VI_DOOMED) == 0, dvp,
	    ("cache_enter: Doomed vnode used as src"));

	if (__predict_false(!doingcache))
		return;

	/*
	 * Avoid blowout in namecache entries.
	 */
	if (__predict_false(numcache >= desiredvnodes * ncsizefactor))
		return;

	cache_celockstate_init(&cel);
	ndd = NULL;
	ncp_ts = NULL;
	flag = 0;
	if (cnp->cn_nameptr[0] == '.') {
		if (cnp->cn_namelen == 1)
			return;
		if (cnp->cn_namelen == 2 && cnp->cn_nameptr[1] == '.') {
			len = cnp->cn_namelen;
			hash = cache_get_hash(cnp->cn_nameptr, len, dvp);
			cache_enter_lock_dd(&cel, dvp, vp, hash);
			/*
			 * If dotdot entry already exists, just retarget it
			 * to new parent vnode, otherwise continue with new
			 * namecache entry allocation.
			 */
			if ((ncp = dvp->v_cache_dd) != NULL &&
			    ncp->nc_flag & NCF_ISDOTDOT) {
				KASSERT(ncp->nc_dvp == dvp,
				    ("wrong isdotdot parent"));
				neg_locked = false;
				if (ncp->nc_flag & NCF_NEGATIVE || vp == NULL) {
					neglist = NCP2NEGLIST(ncp);
					mtx_lock(&ncneg_hot.nl_lock);
					mtx_lock(&neglist->nl_lock);
					neg_locked = true;
				}
				if (!(ncp->nc_flag & NCF_NEGATIVE)) {
					TAILQ_REMOVE(&ncp->nc_vp->v_cache_dst,
					    ncp, nc_dst);
				} else {
					cache_negative_remove(ncp, true);
				}
				if (vp != NULL) {
					TAILQ_INSERT_HEAD(&vp->v_cache_dst,
					    ncp, nc_dst);
					ncp->nc_flag &= ~(NCF_NEGATIVE|NCF_HOTNEGATIVE);
				} else {
					ncp->nc_flag &= ~(NCF_HOTNEGATIVE);
					ncp->nc_flag |= NCF_NEGATIVE;
					cache_negative_insert(ncp, true);
				}
				if (neg_locked) {
					mtx_unlock(&neglist->nl_lock);
					mtx_unlock(&ncneg_hot.nl_lock);
				}
				ncp->nc_vp = vp;
				cache_enter_unlock(&cel);
				return;
			}
			dvp->v_cache_dd = NULL;
			cache_enter_unlock(&cel);
			cache_celockstate_init(&cel);
			SDT_PROBE3(vfs, namecache, enter, done, dvp, "..", vp);
			flag = NCF_ISDOTDOT;
		}
	}

	/*
	 * Calculate the hash key and setup as much of the new
	 * namecache entry as possible before acquiring the lock.
	 */
	ncp = cache_alloc(cnp->cn_namelen, tsp != NULL);
	ncp->nc_flag = flag;
	ncp->nc_vp = vp;
	if (vp == NULL)
		ncp->nc_flag |= NCF_NEGATIVE;
	ncp->nc_dvp = dvp;
	if (tsp != NULL) {
		ncp_ts = __containerof(ncp, struct namecache_ts, nc_nc);
		ncp_ts->nc_time = *tsp;
		ncp_ts->nc_ticks = ticks;
		ncp_ts->nc_nc.nc_flag |= NCF_TS;
		if (dtsp != NULL) {
			ncp_ts->nc_dotdottime = *dtsp;
			ncp_ts->nc_nc.nc_flag |= NCF_DTS;
		}
	}
	len = ncp->nc_nlen = cnp->cn_namelen;
	hash = cache_get_hash(cnp->cn_nameptr, len, dvp);
	strlcpy(ncp->nc_name, cnp->cn_nameptr, len + 1);
	cache_enter_lock(&cel, dvp, vp, hash);

	/*
	 * See if this vnode or negative entry is already in the cache
	 * with this name.  This can happen with concurrent lookups of
	 * the same path name.
	 */
	ncpp = NCHHASH(hash);
	LIST_FOREACH(n2, ncpp, nc_hash) {
		if (n2->nc_dvp == dvp &&
		    n2->nc_nlen == cnp->cn_namelen &&
		    !bcmp(n2->nc_name, cnp->cn_nameptr, n2->nc_nlen)) {
			if (tsp != NULL) {
				KASSERT((n2->nc_flag & NCF_TS) != 0,
				    ("no NCF_TS"));
				n2_ts = __containerof(n2, struct namecache_ts, nc_nc);
				n2_ts->nc_time = ncp_ts->nc_time;
				n2_ts->nc_ticks = ncp_ts->nc_ticks;
				if (dtsp != NULL) {
					n2_ts->nc_dotdottime = ncp_ts->nc_dotdottime;
					if (ncp->nc_flag & NCF_NEGATIVE)
						mtx_lock(&ncneg_hot.nl_lock);
					n2_ts->nc_nc.nc_flag |= NCF_DTS;
					if (ncp->nc_flag & NCF_NEGATIVE)
						mtx_unlock(&ncneg_hot.nl_lock);
				}
			}
			goto out_unlock_free;
		}
	}

	if (flag == NCF_ISDOTDOT) {
		/*
		 * See if we are trying to add .. entry, but some other lookup
		 * has populated v_cache_dd pointer already.
		 */
		if (dvp->v_cache_dd != NULL)
			goto out_unlock_free;
		KASSERT(vp == NULL || vp->v_type == VDIR,
		    ("wrong vnode type %p", vp));
		dvp->v_cache_dd = ncp;
	}

	if (vp != NULL) {
		if (vp->v_type == VDIR) {
			if (flag != NCF_ISDOTDOT) {
				/*
				 * For this case, the cache entry maps both the
				 * directory name in it and the name ".." for the
				 * directory's parent.
				 */
				if ((ndd = vp->v_cache_dd) != NULL) {
					if ((ndd->nc_flag & NCF_ISDOTDOT) != 0)
						cache_zap_locked(ndd, false);
					else
						ndd = NULL;
				}
				vp->v_cache_dd = ncp;
			}
		} else {
			vp->v_cache_dd = NULL;
		}
	}

	if (flag != NCF_ISDOTDOT) {
		if (LIST_EMPTY(&dvp->v_cache_src)) {
			vhold(dvp);
			atomic_add_rel_long(&numcachehv, 1);
		}
		LIST_INSERT_HEAD(&dvp->v_cache_src, ncp, nc_src);
	}

	/*
	 * Insert the new namecache entry into the appropriate chain
	 * within the cache entries table.
	 */
	LIST_INSERT_HEAD(ncpp, ncp, nc_hash);

	/*
	 * If the entry is "negative", we place it into the
	 * "negative" cache queue, otherwise, we place it into the
	 * destination vnode's cache entries queue.
	 */
	if (vp != NULL) {
		TAILQ_INSERT_HEAD(&vp->v_cache_dst, ncp, nc_dst);
		SDT_PROBE3(vfs, namecache, enter, done, dvp, ncp->nc_name,
		    vp);
	} else {
		if (cnp->cn_flags & ISWHITEOUT)
			ncp->nc_flag |= NCF_WHITE;
		cache_negative_insert(ncp, false);
		SDT_PROBE2(vfs, namecache, enter_negative, done, dvp,
		    ncp->nc_name);
	}
	cache_enter_unlock(&cel);
	lnumcache = atomic_fetchadd_long(&numcache, 1) + 1;
	if (numneg * ncnegfactor > lnumcache)
		cache_negative_zap_one();
	cache_free(ndd);
	return;
out_unlock_free:
	cache_enter_unlock(&cel);
	cache_free(ncp);
	return;
}

static u_int
cache_roundup_2(u_int val)
{
	u_int res;

	for (res = 1; res <= val; res <<= 1)
		continue;

	return (res);
}

/*
 * Name cache initialization, from vfs_init() when we are booting
 */
static void
nchinit(void *dummy __unused)
{
	u_int i;

	cache_zone_small = uma_zcreate("S VFS Cache",
	    sizeof(struct namecache) + CACHE_PATH_CUTOFF + 1,
	    NULL, NULL, NULL, NULL, UMA_ALIGNOF(struct namecache),
	    UMA_ZONE_ZINIT);
	cache_zone_small_ts = uma_zcreate("STS VFS Cache",
	    sizeof(struct namecache_ts) + CACHE_PATH_CUTOFF + 1,
	    NULL, NULL, NULL, NULL, UMA_ALIGNOF(struct namecache_ts),
	    UMA_ZONE_ZINIT);
	cache_zone_large = uma_zcreate("L VFS Cache",
	    sizeof(struct namecache) + NAME_MAX + 1,
	    NULL, NULL, NULL, NULL, UMA_ALIGNOF(struct namecache),
	    UMA_ZONE_ZINIT);
	cache_zone_large_ts = uma_zcreate("LTS VFS Cache",
	    sizeof(struct namecache_ts) + NAME_MAX + 1,
	    NULL, NULL, NULL, NULL, UMA_ALIGNOF(struct namecache_ts),
	    UMA_ZONE_ZINIT);

	nchashtbl = hashinit(desiredvnodes * 2, M_VFSCACHE, &nchash);
	ncbuckethash = cache_roundup_2(mp_ncpus * 64) - 1;
	if (ncbuckethash > nchash)
		ncbuckethash = nchash;
	bucketlocks = malloc(sizeof(*bucketlocks) * numbucketlocks, M_VFSCACHE,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < numbucketlocks; i++)
		rw_init_flags(&bucketlocks[i], "ncbuc", RW_DUPOK | RW_RECURSE);
	ncvnodehash = cache_roundup_2(mp_ncpus * 64) - 1;
	vnodelocks = malloc(sizeof(*vnodelocks) * numvnodelocks, M_VFSCACHE,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < numvnodelocks; i++)
		mtx_init(&vnodelocks[i], "ncvn", NULL, MTX_DUPOK | MTX_RECURSE);
	ncpurgeminvnodes = numbucketlocks;

	ncneghash = 3;
	neglists = malloc(sizeof(*neglists) * numneglists, M_VFSCACHE,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < numneglists; i++) {
		mtx_init(&neglists[i].nl_lock, "ncnegl", NULL, MTX_DEF);
		TAILQ_INIT(&neglists[i].nl_list);
	}
	mtx_init(&ncneg_hot.nl_lock, "ncneglh", NULL, MTX_DEF);
	TAILQ_INIT(&ncneg_hot.nl_list);

	mtx_init(&ncneg_shrink_lock, "ncnegs", NULL, MTX_DEF);

	numcalls = counter_u64_alloc(M_WAITOK);
	dothits = counter_u64_alloc(M_WAITOK);
	dotdothits = counter_u64_alloc(M_WAITOK);
	numchecks = counter_u64_alloc(M_WAITOK);
	nummiss = counter_u64_alloc(M_WAITOK);
	nummisszap = counter_u64_alloc(M_WAITOK);
	numposzaps = counter_u64_alloc(M_WAITOK);
	numposhits = counter_u64_alloc(M_WAITOK);
	numnegzaps = counter_u64_alloc(M_WAITOK);
	numneghits = counter_u64_alloc(M_WAITOK);
	numfullpathcalls = counter_u64_alloc(M_WAITOK);
	numfullpathfail1 = counter_u64_alloc(M_WAITOK);
	numfullpathfail2 = counter_u64_alloc(M_WAITOK);
	numfullpathfail4 = counter_u64_alloc(M_WAITOK);
	numfullpathfound = counter_u64_alloc(M_WAITOK);
}
SYSINIT(vfs, SI_SUB_VFS, SI_ORDER_SECOND, nchinit, NULL);

void
cache_changesize(int newmaxvnodes)
{
	struct nchashhead *new_nchashtbl, *old_nchashtbl;
	u_long new_nchash, old_nchash;
	struct namecache *ncp;
	uint32_t hash;
	int i;

	newmaxvnodes = cache_roundup_2(newmaxvnodes * 2);
	if (newmaxvnodes < numbucketlocks)
		newmaxvnodes = numbucketlocks;

	new_nchashtbl = hashinit(newmaxvnodes, M_VFSCACHE, &new_nchash);
	/* If same hash table size, nothing to do */
	if (nchash == new_nchash) {
		free(new_nchashtbl, M_VFSCACHE);
		return;
	}
	/*
	 * Move everything from the old hash table to the new table.
	 * None of the namecache entries in the table can be removed
	 * because to do so, they have to be removed from the hash table.
	 */
	cache_lock_all_vnodes();
	cache_lock_all_buckets();
	old_nchashtbl = nchashtbl;
	old_nchash = nchash;
	nchashtbl = new_nchashtbl;
	nchash = new_nchash;
	for (i = 0; i <= old_nchash; i++) {
		while ((ncp = LIST_FIRST(&old_nchashtbl[i])) != NULL) {
			hash = cache_get_hash(ncp->nc_name, ncp->nc_nlen,
			    ncp->nc_dvp);
			LIST_REMOVE(ncp, nc_hash);
			LIST_INSERT_HEAD(NCHHASH(hash), ncp, nc_hash);
		}
	}
	cache_unlock_all_buckets();
	cache_unlock_all_vnodes();
	free(old_nchashtbl, M_VFSCACHE);
}

/*
 * Invalidate all entries to a particular vnode.
 */
void
cache_purge(struct vnode *vp)
{
	TAILQ_HEAD(, namecache) ncps;
	struct namecache *ncp, *nnp;
	struct mtx *vlp, *vlp2;

	CTR1(KTR_VFS, "cache_purge(%p)", vp);
	SDT_PROBE1(vfs, namecache, purge, done, vp);
	if (LIST_EMPTY(&vp->v_cache_src) && TAILQ_EMPTY(&vp->v_cache_dst) &&
	    vp->v_cache_dd == NULL)
		return;
	TAILQ_INIT(&ncps);
	vlp = VP2VNODELOCK(vp);
	vlp2 = NULL;
	mtx_lock(vlp);
retry:
	while (!LIST_EMPTY(&vp->v_cache_src)) {
		ncp = LIST_FIRST(&vp->v_cache_src);
		if (!cache_zap_locked_vnode_kl2(ncp, vp, &vlp2))
			goto retry;
		TAILQ_INSERT_TAIL(&ncps, ncp, nc_dst);
	}
	while (!TAILQ_EMPTY(&vp->v_cache_dst)) {
		ncp = TAILQ_FIRST(&vp->v_cache_dst);
		if (!cache_zap_locked_vnode_kl2(ncp, vp, &vlp2))
			goto retry;
		TAILQ_INSERT_TAIL(&ncps, ncp, nc_dst);
	}
	ncp = vp->v_cache_dd;
	if (ncp != NULL) {
		KASSERT(ncp->nc_flag & NCF_ISDOTDOT,
		   ("lost dotdot link"));
		if (!cache_zap_locked_vnode_kl2(ncp, vp, &vlp2))
			goto retry;
		TAILQ_INSERT_TAIL(&ncps, ncp, nc_dst);
	}
	KASSERT(vp->v_cache_dd == NULL, ("incomplete purge"));
	mtx_unlock(vlp);
	if (vlp2 != NULL)
		mtx_unlock(vlp2);
	TAILQ_FOREACH_SAFE(ncp, &ncps, nc_dst, nnp) {
		cache_free(ncp);
	}
}

/*
 * Invalidate all negative entries for a particular directory vnode.
 */
void
cache_purge_negative(struct vnode *vp)
{
	TAILQ_HEAD(, namecache) ncps;
	struct namecache *ncp, *nnp;
	struct mtx *vlp;

	CTR1(KTR_VFS, "cache_purge_negative(%p)", vp);
	SDT_PROBE1(vfs, namecache, purge_negative, done, vp);
	if (LIST_EMPTY(&vp->v_cache_src))
		return;
	TAILQ_INIT(&ncps);
	vlp = VP2VNODELOCK(vp);
	mtx_lock(vlp);
	LIST_FOREACH_SAFE(ncp, &vp->v_cache_src, nc_src, nnp) {
		if (!(ncp->nc_flag & NCF_NEGATIVE))
			continue;
		cache_zap_negative_locked_vnode_kl(ncp, vp);
		TAILQ_INSERT_TAIL(&ncps, ncp, nc_dst);
	}
	mtx_unlock(vlp);
	TAILQ_FOREACH_SAFE(ncp, &ncps, nc_dst, nnp) {
		cache_free(ncp);
	}
}

/*
 * Flush all entries referencing a particular filesystem.
 */
void
cache_purgevfs(struct mount *mp, bool force)
{
	TAILQ_HEAD(, namecache) ncps;
	struct mtx *vlp1, *vlp2;
	struct rwlock *blp;
	struct nchashhead *bucket;
	struct namecache *ncp, *nnp;
	u_long i, j, n_nchash;
	int error;

	/* Scan hash tables for applicable entries */
	SDT_PROBE1(vfs, namecache, purgevfs, done, mp);
	if (!force && mp->mnt_nvnodelistsize <= ncpurgeminvnodes)
		return;
	TAILQ_INIT(&ncps);
	n_nchash = nchash + 1;
	vlp1 = vlp2 = NULL;
	for (i = 0; i < numbucketlocks; i++) {
		blp = (struct rwlock *)&bucketlocks[i];
		rw_wlock(blp);
		for (j = i; j < n_nchash; j += numbucketlocks) {
retry:
			bucket = &nchashtbl[j];
			LIST_FOREACH_SAFE(ncp, bucket, nc_hash, nnp) {
				cache_assert_bucket_locked(ncp, RA_WLOCKED);
				if (ncp->nc_dvp->v_mount != mp)
					continue;
				error = cache_zap_wlocked_bucket_kl(ncp, blp,
				    &vlp1, &vlp2);
				if (error != 0)
					goto retry;
				TAILQ_INSERT_HEAD(&ncps, ncp, nc_dst);
			}
		}
		rw_wunlock(blp);
		if (vlp1 == NULL && vlp2 == NULL)
			cache_maybe_yield();
	}
	if (vlp1 != NULL)
		mtx_unlock(vlp1);
	if (vlp2 != NULL)
		mtx_unlock(vlp2);

	TAILQ_FOREACH_SAFE(ncp, &ncps, nc_dst, nnp) {
		cache_free(ncp);
	}
}

/*
 * Perform canonical checks and cache lookup and pass on to filesystem
 * through the vop_cachedlookup only if needed.
 */

int
vfs_cache_lookup(struct vop_lookup_args *ap)
{
	struct vnode *dvp;
	int error;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct ucred *cred = cnp->cn_cred;
	int flags = cnp->cn_flags;
	struct thread *td = cnp->cn_thread;

	*vpp = NULL;
	dvp = ap->a_dvp;

	if (dvp->v_type != VDIR)
		return (ENOTDIR);

	if ((flags & ISLASTCN) && (dvp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME))
		return (EROFS);

	error = VOP_ACCESS(dvp, VEXEC, cred, td);
	if (error)
		return (error);

	error = cache_lookup(dvp, vpp, cnp, NULL, NULL);
	if (error == 0)
		return (VOP_CACHEDLOOKUP(dvp, vpp, cnp));
	if (error == -1)
		return (0);
	return (error);
}

/*
 * XXX All of these sysctls would probably be more productive dead.
 */
static int __read_mostly disablecwd;
SYSCTL_INT(_debug, OID_AUTO, disablecwd, CTLFLAG_RW, &disablecwd, 0,
   "Disable the getcwd syscall");

/* Implementation of the getcwd syscall. */
int
sys___getcwd(struct thread *td, struct __getcwd_args *uap)
{

	return (kern___getcwd(td, uap->buf, UIO_USERSPACE, uap->buflen,
	    MAXPATHLEN));
}

int
kern___getcwd(struct thread *td, char *buf, enum uio_seg bufseg, size_t buflen,
    size_t path_max)
{
	char *bp, *tmpbuf;
	struct filedesc *fdp;
	struct vnode *cdir, *rdir;
	int error;

	if (__predict_false(disablecwd))
		return (ENODEV);
	if (__predict_false(buflen < 2))
		return (EINVAL);
	if (buflen > path_max)
		buflen = path_max;

	tmpbuf = malloc(buflen, M_TEMP, M_WAITOK);
	fdp = td->td_proc->p_fd;
	FILEDESC_SLOCK(fdp);
	cdir = fdp->fd_cdir;
	vrefact(cdir);
	rdir = fdp->fd_rdir;
	vrefact(rdir);
	FILEDESC_SUNLOCK(fdp);
	error = vn_fullpath1(td, cdir, rdir, tmpbuf, &bp, buflen);
	vrele(rdir);
	vrele(cdir);

	if (!error) {
		if (bufseg == UIO_SYSSPACE)
			bcopy(bp, buf, strlen(bp) + 1);
		else
			error = copyout(bp, buf, strlen(bp) + 1);
#ifdef KTRACE
	if (KTRPOINT(curthread, KTR_NAMEI))
		ktrnamei(bp);
#endif
	}
	free(tmpbuf, M_TEMP);
	return (error);
}

/*
 * Thus begins the fullpath magic.
 */

static int __read_mostly disablefullpath;
SYSCTL_INT(_debug, OID_AUTO, disablefullpath, CTLFLAG_RW, &disablefullpath, 0,
    "Disable the vn_fullpath function");

/*
 * Retrieve the full filesystem path that correspond to a vnode from the name
 * cache (if available)
 */
int
vn_fullpath(struct thread *td, struct vnode *vn, char **retbuf, char **freebuf)
{
	char *buf;
	struct filedesc *fdp;
	struct vnode *rdir;
	int error;

	if (__predict_false(disablefullpath))
		return (ENODEV);
	if (__predict_false(vn == NULL))
		return (EINVAL);

	buf = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	fdp = td->td_proc->p_fd;
	FILEDESC_SLOCK(fdp);
	rdir = fdp->fd_rdir;
	vrefact(rdir);
	FILEDESC_SUNLOCK(fdp);
	error = vn_fullpath1(td, vn, rdir, buf, retbuf, MAXPATHLEN);
	vrele(rdir);

	if (!error)
		*freebuf = buf;
	else
		free(buf, M_TEMP);
	return (error);
}

/*
 * This function is similar to vn_fullpath, but it attempts to lookup the
 * pathname relative to the global root mount point.  This is required for the
 * auditing sub-system, as audited pathnames must be absolute, relative to the
 * global root mount point.
 */
int
vn_fullpath_global(struct thread *td, struct vnode *vn,
    char **retbuf, char **freebuf)
{
	char *buf;
	int error;

	if (__predict_false(disablefullpath))
		return (ENODEV);
	if (__predict_false(vn == NULL))
		return (EINVAL);
	buf = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	error = vn_fullpath1(td, vn, rootvnode, buf, retbuf, MAXPATHLEN);
	if (!error)
		*freebuf = buf;
	else
		free(buf, M_TEMP);
	return (error);
}

int
vn_vptocnp(struct vnode **vp, struct ucred *cred, char *buf, u_int *buflen)
{
	struct vnode *dvp;
	struct namecache *ncp;
	struct mtx *vlp;
	int error;

	vlp = VP2VNODELOCK(*vp);
	mtx_lock(vlp);
	TAILQ_FOREACH(ncp, &((*vp)->v_cache_dst), nc_dst) {
		if ((ncp->nc_flag & NCF_ISDOTDOT) == 0)
			break;
	}
	if (ncp != NULL) {
		if (*buflen < ncp->nc_nlen) {
			mtx_unlock(vlp);
			vrele(*vp);
			counter_u64_add(numfullpathfail4, 1);
			error = ENOMEM;
			SDT_PROBE3(vfs, namecache, fullpath, return, error,
			    vp, NULL);
			return (error);
		}
		*buflen -= ncp->nc_nlen;
		memcpy(buf + *buflen, ncp->nc_name, ncp->nc_nlen);
		SDT_PROBE3(vfs, namecache, fullpath, hit, ncp->nc_dvp,
		    ncp->nc_name, vp);
		dvp = *vp;
		*vp = ncp->nc_dvp;
		vref(*vp);
		mtx_unlock(vlp);
		vrele(dvp);
		return (0);
	}
	SDT_PROBE1(vfs, namecache, fullpath, miss, vp);

	mtx_unlock(vlp);
	vn_lock(*vp, LK_SHARED | LK_RETRY);
	error = VOP_VPTOCNP(*vp, &dvp, cred, buf, buflen);
	vput(*vp);
	if (error) {
		counter_u64_add(numfullpathfail2, 1);
		SDT_PROBE3(vfs, namecache, fullpath, return,  error, vp, NULL);
		return (error);
	}

	*vp = dvp;
	if (dvp->v_iflag & VI_DOOMED) {
		/* forced unmount */
		vrele(dvp);
		error = ENOENT;
		SDT_PROBE3(vfs, namecache, fullpath, return, error, vp, NULL);
		return (error);
	}
	/*
	 * *vp has its use count incremented still.
	 */

	return (0);
}

/*
 * The magic behind kern___getcwd() and vn_fullpath().
 */
static int
vn_fullpath1(struct thread *td, struct vnode *vp, struct vnode *rdir,
    char *buf, char **retbuf, u_int buflen)
{
	int error, slash_prefixed;
#ifdef KDTRACE_HOOKS
	struct vnode *startvp = vp;
#endif
	struct vnode *vp1;

	buflen--;
	buf[buflen] = '\0';
	error = 0;
	slash_prefixed = 0;

	SDT_PROBE1(vfs, namecache, fullpath, entry, vp);
	counter_u64_add(numfullpathcalls, 1);
	vref(vp);
	if (vp->v_type != VDIR) {
		error = vn_vptocnp(&vp, td->td_ucred, buf, &buflen);
		if (error)
			return (error);
		if (buflen == 0) {
			vrele(vp);
			return (ENOMEM);
		}
		buf[--buflen] = '/';
		slash_prefixed = 1;
	}
	while (vp != rdir && vp != rootvnode) {
		/*
		 * The vp vnode must be already fully constructed,
		 * since it is either found in namecache or obtained
		 * from VOP_VPTOCNP().  We may test for VV_ROOT safely
		 * without obtaining the vnode lock.
		 */
		if ((vp->v_vflag & VV_ROOT) != 0) {
			vn_lock(vp, LK_RETRY | LK_SHARED);

			/*
			 * With the vnode locked, check for races with
			 * unmount, forced or not.  Note that we
			 * already verified that vp is not equal to
			 * the root vnode, which means that
			 * mnt_vnodecovered can be NULL only for the
			 * case of unmount.
			 */
			if ((vp->v_iflag & VI_DOOMED) != 0 ||
			    (vp1 = vp->v_mount->mnt_vnodecovered) == NULL ||
			    vp1->v_mountedhere != vp->v_mount) {
				vput(vp);
				error = ENOENT;
				SDT_PROBE3(vfs, namecache, fullpath, return,
				    error, vp, NULL);
				break;
			}

			vref(vp1);
			vput(vp);
			vp = vp1;
			continue;
		}
		if (vp->v_type != VDIR) {
			vrele(vp);
			counter_u64_add(numfullpathfail1, 1);
			error = ENOTDIR;
			SDT_PROBE3(vfs, namecache, fullpath, return,
			    error, vp, NULL);
			break;
		}
		error = vn_vptocnp(&vp, td->td_ucred, buf, &buflen);
		if (error)
			break;
		if (buflen == 0) {
			vrele(vp);
			error = ENOMEM;
			SDT_PROBE3(vfs, namecache, fullpath, return, error,
			    startvp, NULL);
			break;
		}
		buf[--buflen] = '/';
		slash_prefixed = 1;
	}
	if (error)
		return (error);
	if (!slash_prefixed) {
		if (buflen == 0) {
			vrele(vp);
			counter_u64_add(numfullpathfail4, 1);
			SDT_PROBE3(vfs, namecache, fullpath, return, ENOMEM,
			    startvp, NULL);
			return (ENOMEM);
		}
		buf[--buflen] = '/';
	}
	counter_u64_add(numfullpathfound, 1);
	vrele(vp);

	SDT_PROBE3(vfs, namecache, fullpath, return, 0, startvp, buf + buflen);
	*retbuf = buf + buflen;
	return (0);
}

struct vnode *
vn_dir_dd_ino(struct vnode *vp)
{
	struct namecache *ncp;
	struct vnode *ddvp;
	struct mtx *vlp;

	ASSERT_VOP_LOCKED(vp, "vn_dir_dd_ino");
	vlp = VP2VNODELOCK(vp);
	mtx_lock(vlp);
	TAILQ_FOREACH(ncp, &(vp->v_cache_dst), nc_dst) {
		if ((ncp->nc_flag & NCF_ISDOTDOT) != 0)
			continue;
		ddvp = ncp->nc_dvp;
		vhold(ddvp);
		mtx_unlock(vlp);
		if (vget(ddvp, LK_SHARED | LK_NOWAIT | LK_VNHELD, curthread))
			return (NULL);
		return (ddvp);
	}
	mtx_unlock(vlp);
	return (NULL);
}

int
vn_commname(struct vnode *vp, char *buf, u_int buflen)
{
	struct namecache *ncp;
	struct mtx *vlp;
	int l;

	vlp = VP2VNODELOCK(vp);
	mtx_lock(vlp);
	TAILQ_FOREACH(ncp, &vp->v_cache_dst, nc_dst)
		if ((ncp->nc_flag & NCF_ISDOTDOT) == 0)
			break;
	if (ncp == NULL) {
		mtx_unlock(vlp);
		return (ENOENT);
	}
	l = min(ncp->nc_nlen, buflen - 1);
	memcpy(buf, ncp->nc_name, l);
	mtx_unlock(vlp);
	buf[l] = '\0';
	return (0);
}

/*
 * This function updates path string to vnode's full global path
 * and checks the size of the new path string against the pathlen argument.
 *
 * Requires a locked, referenced vnode.
 * Vnode is re-locked on success or ENODEV, otherwise unlocked.
 *
 * If sysctl debug.disablefullpath is set, ENODEV is returned,
 * vnode is left locked and path remain untouched.
 *
 * If vp is a directory, the call to vn_fullpath_global() always succeeds
 * because it falls back to the ".." lookup if the namecache lookup fails.
 */
int
vn_path_to_global_path(struct thread *td, struct vnode *vp, char *path,
    u_int pathlen)
{
	struct nameidata nd;
	struct vnode *vp1;
	char *rpath, *fbuf;
	int error;

	ASSERT_VOP_ELOCKED(vp, __func__);

	/* Return ENODEV if sysctl debug.disablefullpath==1 */
	if (__predict_false(disablefullpath))
		return (ENODEV);

	/* Construct global filesystem path from vp. */
	VOP_UNLOCK(vp, 0);
	error = vn_fullpath_global(td, vp, &rpath, &fbuf);

	if (error != 0) {
		vrele(vp);
		return (error);
	}

	if (strlen(rpath) >= pathlen) {
		vrele(vp);
		error = ENAMETOOLONG;
		goto out;
	}

	/*
	 * Re-lookup the vnode by path to detect a possible rename.
	 * As a side effect, the vnode is relocked.
	 * If vnode was renamed, return ENOENT.
	 */
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNODE1,
	    UIO_SYSSPACE, path, td);
	error = namei(&nd);
	if (error != 0) {
		vrele(vp);
		goto out;
	}
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vp1 = nd.ni_vp;
	vrele(vp);
	if (vp1 == vp)
		strcpy(path, rpath);
	else {
		vput(vp1);
		error = ENOENT;
	}

out:
	free(fbuf, M_TEMP);
	return (error);
}

#ifdef DDB
static void
db_print_vpath(struct vnode *vp)
{

	while (vp != NULL) {
		db_printf("%p: ", vp);
		if (vp == rootvnode) {
			db_printf("/");
			vp = NULL;
		} else {
			if (vp->v_vflag & VV_ROOT) {
				db_printf("<mount point>");
				vp = vp->v_mount->mnt_vnodecovered;
			} else {
				struct namecache *ncp;
				char *ncn;
				int i;

				ncp = TAILQ_FIRST(&vp->v_cache_dst);
				if (ncp != NULL) {
					ncn = ncp->nc_name;
					for (i = 0; i < ncp->nc_nlen; i++)
						db_printf("%c", *ncn++);
					vp = ncp->nc_dvp;
				} else {
					vp = NULL;
				}
			}
		}
		db_printf("\n");
	}

	return;
}

DB_SHOW_COMMAND(vpath, db_show_vpath)
{
	struct vnode *vp;

	if (!have_addr) {
		db_printf("usage: show vpath <struct vnode *>\n");
		return;
	}

	vp = (struct vnode *)addr;
	db_print_vpath(vp);
}

#endif
