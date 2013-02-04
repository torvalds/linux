/*
 * Request reply cache. This is currently a global cache, but this may
 * change in the future and be a per-client cache.
 *
 * This code is heavily inspired by the 44BSD implementation, although
 * it does things a bit differently.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/slab.h>
#include <linux/sunrpc/clnt.h>
#include <linux/highmem.h>

#include "nfsd.h"
#include "cache.h"

#define NFSDDBG_FACILITY	NFSDDBG_REPCACHE

#define HASHSIZE		64

static struct hlist_head *	cache_hash;
static struct list_head 	lru_head;
static struct kmem_cache	*drc_slab;
static unsigned int		num_drc_entries;
static unsigned int		max_drc_entries;

/*
 * Calculate the hash index from an XID.
 */
static inline u32 request_hash(u32 xid)
{
	u32 h = xid;
	h ^= (xid >> 24);
	return h & (HASHSIZE-1);
}

static int	nfsd_cache_append(struct svc_rqst *rqstp, struct kvec *vec);
static void	cache_cleaner_func(struct work_struct *unused);

/*
 * locking for the reply cache:
 * A cache entry is "single use" if c_state == RC_INPROG
 * Otherwise, it when accessing _prev or _next, the lock must be held.
 */
static DEFINE_SPINLOCK(cache_lock);
static DECLARE_DELAYED_WORK(cache_cleaner, cache_cleaner_func);

/*
 * Put a cap on the size of the DRC based on the amount of available
 * low memory in the machine.
 *
 *  64MB:    8192
 * 128MB:   11585
 * 256MB:   16384
 * 512MB:   23170
 *   1GB:   32768
 *   2GB:   46340
 *   4GB:   65536
 *   8GB:   92681
 *  16GB:  131072
 *
 * ...with a hard cap of 256k entries. In the worst case, each entry will be
 * ~1k, so the above numbers should give a rough max of the amount of memory
 * used in k.
 */
static unsigned int
nfsd_cache_size_limit(void)
{
	unsigned int limit;
	unsigned long low_pages = totalram_pages - totalhigh_pages;

	limit = (16 * int_sqrt(low_pages)) << (PAGE_SHIFT-10);
	return min_t(unsigned int, limit, 256*1024);
}

static struct svc_cacherep *
nfsd_reply_cache_alloc(void)
{
	struct svc_cacherep	*rp;

	rp = kmem_cache_alloc(drc_slab, GFP_KERNEL);
	if (rp) {
		rp->c_state = RC_UNUSED;
		rp->c_type = RC_NOCACHE;
		INIT_LIST_HEAD(&rp->c_lru);
		INIT_HLIST_NODE(&rp->c_hash);
	}
	return rp;
}

static void
nfsd_reply_cache_free_locked(struct svc_cacherep *rp)
{
	if (rp->c_type == RC_REPLBUFF)
		kfree(rp->c_replvec.iov_base);
	hlist_del(&rp->c_hash);
	list_del(&rp->c_lru);
	--num_drc_entries;
	kmem_cache_free(drc_slab, rp);
}

static void
nfsd_reply_cache_free(struct svc_cacherep *rp)
{
	spin_lock(&cache_lock);
	nfsd_reply_cache_free_locked(rp);
	spin_unlock(&cache_lock);
}

int nfsd_reply_cache_init(void)
{
	drc_slab = kmem_cache_create("nfsd_drc", sizeof(struct svc_cacherep),
					0, 0, NULL);
	if (!drc_slab)
		goto out_nomem;

	cache_hash = kcalloc(HASHSIZE, sizeof(struct hlist_head), GFP_KERNEL);
	if (!cache_hash)
		goto out_nomem;

	INIT_LIST_HEAD(&lru_head);
	max_drc_entries = nfsd_cache_size_limit();
	num_drc_entries = 0;
	return 0;
out_nomem:
	printk(KERN_ERR "nfsd: failed to allocate reply cache\n");
	nfsd_reply_cache_shutdown();
	return -ENOMEM;
}

void nfsd_reply_cache_shutdown(void)
{
	struct svc_cacherep	*rp;

	cancel_delayed_work_sync(&cache_cleaner);

	while (!list_empty(&lru_head)) {
		rp = list_entry(lru_head.next, struct svc_cacherep, c_lru);
		nfsd_reply_cache_free_locked(rp);
	}

	kfree (cache_hash);
	cache_hash = NULL;

	if (drc_slab) {
		kmem_cache_destroy(drc_slab);
		drc_slab = NULL;
	}
}

/*
 * Move cache entry to end of LRU list, and queue the cleaner to run if it's
 * not already scheduled.
 */
static void
lru_put_end(struct svc_cacherep *rp)
{
	rp->c_timestamp = jiffies;
	list_move_tail(&rp->c_lru, &lru_head);
	schedule_delayed_work(&cache_cleaner, RC_EXPIRE);
}

/*
 * Move a cache entry from one hash list to another
 */
static void
hash_refile(struct svc_cacherep *rp)
{
	hlist_del_init(&rp->c_hash);
	hlist_add_head(&rp->c_hash, cache_hash + request_hash(rp->c_xid));
}

static inline bool
nfsd_cache_entry_expired(struct svc_cacherep *rp)
{
	return rp->c_state != RC_INPROG &&
	       time_after(jiffies, rp->c_timestamp + RC_EXPIRE);
}

/*
 * Walk the LRU list and prune off entries that are older than RC_EXPIRE.
 * Also prune the oldest ones when the total exceeds the max number of entries.
 */
static void
prune_cache_entries(void)
{
	struct svc_cacherep *rp, *tmp;

	list_for_each_entry_safe(rp, tmp, &lru_head, c_lru) {
		if (!nfsd_cache_entry_expired(rp) &&
		    num_drc_entries <= max_drc_entries)
			break;
		nfsd_reply_cache_free_locked(rp);
	}

	/*
	 * Conditionally rearm the job. If we cleaned out the list, then
	 * cancel any pending run (since there won't be any work to do).
	 * Otherwise, we rearm the job or modify the existing one to run in
	 * RC_EXPIRE since we just ran the pruner.
	 */
	if (list_empty(&lru_head))
		cancel_delayed_work(&cache_cleaner);
	else
		mod_delayed_work(system_wq, &cache_cleaner, RC_EXPIRE);
}

static void
cache_cleaner_func(struct work_struct *unused)
{
	spin_lock(&cache_lock);
	prune_cache_entries();
	spin_unlock(&cache_lock);
}

/*
 * Search the request hash for an entry that matches the given rqstp.
 * Must be called with cache_lock held. Returns the found entry or
 * NULL on failure.
 */
static struct svc_cacherep *
nfsd_cache_search(struct svc_rqst *rqstp)
{
	struct svc_cacherep	*rp;
	struct hlist_node	*hn;
	struct hlist_head 	*rh;
	__be32			xid = rqstp->rq_xid;
	u32			proto =  rqstp->rq_prot,
				vers = rqstp->rq_vers,
				proc = rqstp->rq_proc;

	rh = &cache_hash[request_hash(xid)];
	hlist_for_each_entry(rp, hn, rh, c_hash) {
		if (xid == rp->c_xid && proc == rp->c_proc &&
		    proto == rp->c_prot && vers == rp->c_vers &&
		    rpc_cmp_addr(svc_addr(rqstp), (struct sockaddr *)&rp->c_addr) &&
		    rpc_get_port(svc_addr(rqstp)) == rpc_get_port((struct sockaddr *)&rp->c_addr))
			return rp;
	}
	return NULL;
}

/*
 * Try to find an entry matching the current call in the cache. When none
 * is found, we grab the oldest unlocked entry off the LRU list.
 * Note that no operation within the loop may sleep.
 */
int
nfsd_cache_lookup(struct svc_rqst *rqstp)
{
	struct svc_cacherep	*rp, *found;
	__be32			xid = rqstp->rq_xid;
	u32			proto =  rqstp->rq_prot,
				vers = rqstp->rq_vers,
				proc = rqstp->rq_proc;
	unsigned long		age;
	int type = rqstp->rq_cachetype;
	int rtn;

	rqstp->rq_cacherep = NULL;
	if (type == RC_NOCACHE) {
		nfsdstats.rcnocache++;
		return RC_DOIT;
	}

	spin_lock(&cache_lock);
	rtn = RC_DOIT;

	rp = nfsd_cache_search(rqstp);
	if (rp)
		goto found_entry;

	/* Try to use the first entry on the LRU */
	if (!list_empty(&lru_head)) {
		rp = list_first_entry(&lru_head, struct svc_cacherep, c_lru);
		if (nfsd_cache_entry_expired(rp) ||
		    num_drc_entries >= max_drc_entries) {
			lru_put_end(rp);
			prune_cache_entries();
			goto setup_entry;
		}
	}

	spin_unlock(&cache_lock);
	rp = nfsd_reply_cache_alloc();
	if (!rp) {
		dprintk("nfsd: unable to allocate DRC entry!\n");
		return RC_DOIT;
	}
	spin_lock(&cache_lock);
	++num_drc_entries;

	/*
	 * Must search again just in case someone inserted one
	 * after we dropped the lock above.
	 */
	found = nfsd_cache_search(rqstp);
	if (found) {
		nfsd_reply_cache_free_locked(rp);
		rp = found;
		goto found_entry;
	}

	/*
	 * We're keeping the one we just allocated. Are we now over the
	 * limit? Prune one off the tip of the LRU in trade for the one we
	 * just allocated if so.
	 */
	if (num_drc_entries >= max_drc_entries)
		nfsd_reply_cache_free_locked(list_first_entry(&lru_head,
						struct svc_cacherep, c_lru));

setup_entry:
	nfsdstats.rcmisses++;
	rqstp->rq_cacherep = rp;
	rp->c_state = RC_INPROG;
	rp->c_xid = xid;
	rp->c_proc = proc;
	rpc_copy_addr((struct sockaddr *)&rp->c_addr, svc_addr(rqstp));
	rpc_set_port((struct sockaddr *)&rp->c_addr, rpc_get_port(svc_addr(rqstp)));
	rp->c_prot = proto;
	rp->c_vers = vers;

	hash_refile(rp);
	lru_put_end(rp);

	/* release any buffer */
	if (rp->c_type == RC_REPLBUFF) {
		kfree(rp->c_replvec.iov_base);
		rp->c_replvec.iov_base = NULL;
	}
	rp->c_type = RC_NOCACHE;
 out:
	spin_unlock(&cache_lock);
	return rtn;

found_entry:
	nfsdstats.rchits++;
	/* We found a matching entry which is either in progress or done. */
	age = jiffies - rp->c_timestamp;
	lru_put_end(rp);

	rtn = RC_DROPIT;
	/* Request being processed or excessive rexmits */
	if (rp->c_state == RC_INPROG || age < RC_DELAY)
		goto out;

	/* From the hall of fame of impractical attacks:
	 * Is this a user who tries to snoop on the cache? */
	rtn = RC_DOIT;
	if (!rqstp->rq_secure && rp->c_secure)
		goto out;

	/* Compose RPC reply header */
	switch (rp->c_type) {
	case RC_NOCACHE:
		break;
	case RC_REPLSTAT:
		svc_putu32(&rqstp->rq_res.head[0], rp->c_replstat);
		rtn = RC_REPLY;
		break;
	case RC_REPLBUFF:
		if (!nfsd_cache_append(rqstp, &rp->c_replvec))
			goto out;	/* should not happen */
		rtn = RC_REPLY;
		break;
	default:
		printk(KERN_WARNING "nfsd: bad repcache type %d\n", rp->c_type);
		nfsd_reply_cache_free_locked(rp);
	}

	goto out;
}

/*
 * Update a cache entry. This is called from nfsd_dispatch when
 * the procedure has been executed and the complete reply is in
 * rqstp->rq_res.
 *
 * We're copying around data here rather than swapping buffers because
 * the toplevel loop requires max-sized buffers, which would be a waste
 * of memory for a cache with a max reply size of 100 bytes (diropokres).
 *
 * If we should start to use different types of cache entries tailored
 * specifically for attrstat and fh's, we may save even more space.
 *
 * Also note that a cachetype of RC_NOCACHE can legally be passed when
 * nfsd failed to encode a reply that otherwise would have been cached.
 * In this case, nfsd_cache_update is called with statp == NULL.
 */
void
nfsd_cache_update(struct svc_rqst *rqstp, int cachetype, __be32 *statp)
{
	struct svc_cacherep *rp = rqstp->rq_cacherep;
	struct kvec	*resv = &rqstp->rq_res.head[0], *cachv;
	int		len;

	if (!rp)
		return;

	len = resv->iov_len - ((char*)statp - (char*)resv->iov_base);
	len >>= 2;

	/* Don't cache excessive amounts of data and XDR failures */
	if (!statp || len > (256 >> 2)) {
		nfsd_reply_cache_free(rp);
		return;
	}

	switch (cachetype) {
	case RC_REPLSTAT:
		if (len != 1)
			printk("nfsd: RC_REPLSTAT/reply len %d!\n",len);
		rp->c_replstat = *statp;
		break;
	case RC_REPLBUFF:
		cachv = &rp->c_replvec;
		cachv->iov_base = kmalloc(len << 2, GFP_KERNEL);
		if (!cachv->iov_base) {
			nfsd_reply_cache_free(rp);
			return;
		}
		cachv->iov_len = len << 2;
		memcpy(cachv->iov_base, statp, len << 2);
		break;
	case RC_NOCACHE:
		nfsd_reply_cache_free(rp);
		return;
	}
	spin_lock(&cache_lock);
	lru_put_end(rp);
	rp->c_secure = rqstp->rq_secure;
	rp->c_type = cachetype;
	rp->c_state = RC_DONE;
	spin_unlock(&cache_lock);
	return;
}

/*
 * Copy cached reply to current reply buffer. Should always fit.
 * FIXME as reply is in a page, we should just attach the page, and
 * keep a refcount....
 */
static int
nfsd_cache_append(struct svc_rqst *rqstp, struct kvec *data)
{
	struct kvec	*vec = &rqstp->rq_res.head[0];

	if (vec->iov_len + data->iov_len > PAGE_SIZE) {
		printk(KERN_WARNING "nfsd: cached reply too large (%Zd).\n",
				data->iov_len);
		return 0;
	}
	memcpy((char*)vec->iov_base + vec->iov_len, data->iov_base, data->iov_len);
	vec->iov_len += data->iov_len;
	return 1;
}
