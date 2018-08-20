// SPDX-License-Identifier: GPL-2.0
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
#include <linux/vmalloc.h>
#include <linux/sunrpc/addr.h>
#include <linux/highmem.h>
#include <linux/log2.h>
#include <linux/hash.h>
#include <net/checksum.h>

#include "nfsd.h"
#include "cache.h"

#define NFSDDBG_FACILITY	NFSDDBG_REPCACHE

/*
 * We use this value to determine the number of hash buckets from the max
 * cache size, the idea being that when the cache is at its maximum number
 * of entries, then this should be the average number of entries per bucket.
 */
#define TARGET_BUCKET_SIZE	64

struct nfsd_drc_bucket {
	struct list_head lru_head;
	spinlock_t cache_lock;
};

static struct nfsd_drc_bucket	*drc_hashtbl;
static struct kmem_cache	*drc_slab;

/* max number of entries allowed in the cache */
static unsigned int		max_drc_entries;

/* number of significant bits in the hash value */
static unsigned int		maskbits;
static unsigned int		drc_hashsize;

/*
 * Stats and other tracking of on the duplicate reply cache. All of these and
 * the "rc" fields in nfsdstats are protected by the cache_lock
 */

/* total number of entries */
static atomic_t			num_drc_entries;

/* cache misses due only to checksum comparison failures */
static unsigned int		payload_misses;

/* amount of memory (in bytes) currently consumed by the DRC */
static unsigned int		drc_mem_usage;

/* longest hash chain seen */
static unsigned int		longest_chain;

/* size of cache when we saw the longest hash chain */
static unsigned int		longest_chain_cachesize;

static int	nfsd_cache_append(struct svc_rqst *rqstp, struct kvec *vec);
static unsigned long nfsd_reply_cache_count(struct shrinker *shrink,
					    struct shrink_control *sc);
static unsigned long nfsd_reply_cache_scan(struct shrinker *shrink,
					   struct shrink_control *sc);

static struct shrinker nfsd_reply_cache_shrinker = {
	.scan_objects = nfsd_reply_cache_scan,
	.count_objects = nfsd_reply_cache_count,
	.seeks	= 1,
};

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

/*
 * Compute the number of hash buckets we need. Divide the max cachesize by
 * the "target" max bucket size, and round up to next power of two.
 */
static unsigned int
nfsd_hashsize(unsigned int limit)
{
	return roundup_pow_of_two(limit / TARGET_BUCKET_SIZE);
}

static u32
nfsd_cache_hash(__be32 xid)
{
	return hash_32(be32_to_cpu(xid), maskbits);
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
	}
	return rp;
}

static void
nfsd_reply_cache_free_locked(struct svc_cacherep *rp)
{
	if (rp->c_type == RC_REPLBUFF && rp->c_replvec.iov_base) {
		drc_mem_usage -= rp->c_replvec.iov_len;
		kfree(rp->c_replvec.iov_base);
	}
	list_del(&rp->c_lru);
	atomic_dec(&num_drc_entries);
	drc_mem_usage -= sizeof(*rp);
	kmem_cache_free(drc_slab, rp);
}

static void
nfsd_reply_cache_free(struct nfsd_drc_bucket *b, struct svc_cacherep *rp)
{
	spin_lock(&b->cache_lock);
	nfsd_reply_cache_free_locked(rp);
	spin_unlock(&b->cache_lock);
}

int nfsd_reply_cache_init(void)
{
	unsigned int hashsize;
	unsigned int i;
	int status = 0;

	max_drc_entries = nfsd_cache_size_limit();
	atomic_set(&num_drc_entries, 0);
	hashsize = nfsd_hashsize(max_drc_entries);
	maskbits = ilog2(hashsize);

	status = register_shrinker(&nfsd_reply_cache_shrinker);
	if (status)
		return status;

	drc_slab = kmem_cache_create("nfsd_drc", sizeof(struct svc_cacherep),
					0, 0, NULL);
	if (!drc_slab)
		goto out_nomem;

	drc_hashtbl = kcalloc(hashsize, sizeof(*drc_hashtbl), GFP_KERNEL);
	if (!drc_hashtbl) {
		drc_hashtbl = vzalloc(array_size(hashsize,
						 sizeof(*drc_hashtbl)));
		if (!drc_hashtbl)
			goto out_nomem;
	}

	for (i = 0; i < hashsize; i++) {
		INIT_LIST_HEAD(&drc_hashtbl[i].lru_head);
		spin_lock_init(&drc_hashtbl[i].cache_lock);
	}
	drc_hashsize = hashsize;

	return 0;
out_nomem:
	printk(KERN_ERR "nfsd: failed to allocate reply cache\n");
	nfsd_reply_cache_shutdown();
	return -ENOMEM;
}

void nfsd_reply_cache_shutdown(void)
{
	struct svc_cacherep	*rp;
	unsigned int i;

	unregister_shrinker(&nfsd_reply_cache_shrinker);

	for (i = 0; i < drc_hashsize; i++) {
		struct list_head *head = &drc_hashtbl[i].lru_head;
		while (!list_empty(head)) {
			rp = list_first_entry(head, struct svc_cacherep, c_lru);
			nfsd_reply_cache_free_locked(rp);
		}
	}

	kvfree(drc_hashtbl);
	drc_hashtbl = NULL;
	drc_hashsize = 0;

	kmem_cache_destroy(drc_slab);
	drc_slab = NULL;
}

/*
 * Move cache entry to end of LRU list, and queue the cleaner to run if it's
 * not already scheduled.
 */
static void
lru_put_end(struct nfsd_drc_bucket *b, struct svc_cacherep *rp)
{
	rp->c_timestamp = jiffies;
	list_move_tail(&rp->c_lru, &b->lru_head);
}

static long
prune_bucket(struct nfsd_drc_bucket *b)
{
	struct svc_cacherep *rp, *tmp;
	long freed = 0;

	list_for_each_entry_safe(rp, tmp, &b->lru_head, c_lru) {
		/*
		 * Don't free entries attached to calls that are still
		 * in-progress, but do keep scanning the list.
		 */
		if (rp->c_state == RC_INPROG)
			continue;
		if (atomic_read(&num_drc_entries) <= max_drc_entries &&
		    time_before(jiffies, rp->c_timestamp + RC_EXPIRE))
			break;
		nfsd_reply_cache_free_locked(rp);
		freed++;
	}
	return freed;
}

/*
 * Walk the LRU list and prune off entries that are older than RC_EXPIRE.
 * Also prune the oldest ones when the total exceeds the max number of entries.
 */
static long
prune_cache_entries(void)
{
	unsigned int i;
	long freed = 0;

	for (i = 0; i < drc_hashsize; i++) {
		struct nfsd_drc_bucket *b = &drc_hashtbl[i];

		if (list_empty(&b->lru_head))
			continue;
		spin_lock(&b->cache_lock);
		freed += prune_bucket(b);
		spin_unlock(&b->cache_lock);
	}
	return freed;
}

static unsigned long
nfsd_reply_cache_count(struct shrinker *shrink, struct shrink_control *sc)
{
	return atomic_read(&num_drc_entries);
}

static unsigned long
nfsd_reply_cache_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	return prune_cache_entries();
}
/*
 * Walk an xdr_buf and get a CRC for at most the first RC_CSUMLEN bytes
 */
static __wsum
nfsd_cache_csum(struct svc_rqst *rqstp)
{
	int idx;
	unsigned int base;
	__wsum csum;
	struct xdr_buf *buf = &rqstp->rq_arg;
	const unsigned char *p = buf->head[0].iov_base;
	size_t csum_len = min_t(size_t, buf->head[0].iov_len + buf->page_len,
				RC_CSUMLEN);
	size_t len = min(buf->head[0].iov_len, csum_len);

	/* rq_arg.head first */
	csum = csum_partial(p, len, 0);
	csum_len -= len;

	/* Continue into page array */
	idx = buf->page_base / PAGE_SIZE;
	base = buf->page_base & ~PAGE_MASK;
	while (csum_len) {
		p = page_address(buf->pages[idx]) + base;
		len = min_t(size_t, PAGE_SIZE - base, csum_len);
		csum = csum_partial(p, len, csum);
		csum_len -= len;
		base = 0;
		++idx;
	}
	return csum;
}

static bool
nfsd_cache_match(struct svc_rqst *rqstp, __wsum csum, struct svc_cacherep *rp)
{
	/* Check RPC XID first */
	if (rqstp->rq_xid != rp->c_xid)
		return false;
	/* compare checksum of NFS data */
	if (csum != rp->c_csum) {
		++payload_misses;
		return false;
	}

	/* Other discriminators */
	if (rqstp->rq_proc != rp->c_proc ||
	    rqstp->rq_prot != rp->c_prot ||
	    rqstp->rq_vers != rp->c_vers ||
	    rqstp->rq_arg.len != rp->c_len ||
	    !rpc_cmp_addr(svc_addr(rqstp), (struct sockaddr *)&rp->c_addr) ||
	    rpc_get_port(svc_addr(rqstp)) != rpc_get_port((struct sockaddr *)&rp->c_addr))
		return false;

	return true;
}

/*
 * Search the request hash for an entry that matches the given rqstp.
 * Must be called with cache_lock held. Returns the found entry or
 * NULL on failure.
 */
static struct svc_cacherep *
nfsd_cache_search(struct nfsd_drc_bucket *b, struct svc_rqst *rqstp,
		__wsum csum)
{
	struct svc_cacherep	*rp, *ret = NULL;
	struct list_head 	*rh = &b->lru_head;
	unsigned int		entries = 0;

	list_for_each_entry(rp, rh, c_lru) {
		++entries;
		if (nfsd_cache_match(rqstp, csum, rp)) {
			ret = rp;
			break;
		}
	}

	/* tally hash chain length stats */
	if (entries > longest_chain) {
		longest_chain = entries;
		longest_chain_cachesize = atomic_read(&num_drc_entries);
	} else if (entries == longest_chain) {
		/* prefer to keep the smallest cachesize possible here */
		longest_chain_cachesize = min_t(unsigned int,
				longest_chain_cachesize,
				atomic_read(&num_drc_entries));
	}

	return ret;
}

/*
 * Try to find an entry matching the current call in the cache. When none
 * is found, we try to grab the oldest expired entry off the LRU list. If
 * a suitable one isn't there, then drop the cache_lock and allocate a
 * new one, then search again in case one got inserted while this thread
 * didn't hold the lock.
 */
int
nfsd_cache_lookup(struct svc_rqst *rqstp)
{
	struct svc_cacherep	*rp, *found;
	__be32			xid = rqstp->rq_xid;
	u32			proto =  rqstp->rq_prot,
				vers = rqstp->rq_vers,
				proc = rqstp->rq_proc;
	__wsum			csum;
	u32 hash = nfsd_cache_hash(xid);
	struct nfsd_drc_bucket *b = &drc_hashtbl[hash];
	int type = rqstp->rq_cachetype;
	int rtn = RC_DOIT;

	rqstp->rq_cacherep = NULL;
	if (type == RC_NOCACHE) {
		nfsdstats.rcnocache++;
		return rtn;
	}

	csum = nfsd_cache_csum(rqstp);

	/*
	 * Since the common case is a cache miss followed by an insert,
	 * preallocate an entry.
	 */
	rp = nfsd_reply_cache_alloc();
	spin_lock(&b->cache_lock);
	if (likely(rp)) {
		atomic_inc(&num_drc_entries);
		drc_mem_usage += sizeof(*rp);
	}

	/* go ahead and prune the cache */
	prune_bucket(b);

	found = nfsd_cache_search(b, rqstp, csum);
	if (found) {
		if (likely(rp))
			nfsd_reply_cache_free_locked(rp);
		rp = found;
		goto found_entry;
	}

	if (!rp) {
		dprintk("nfsd: unable to allocate DRC entry!\n");
		goto out;
	}

	nfsdstats.rcmisses++;
	rqstp->rq_cacherep = rp;
	rp->c_state = RC_INPROG;
	rp->c_xid = xid;
	rp->c_proc = proc;
	rpc_copy_addr((struct sockaddr *)&rp->c_addr, svc_addr(rqstp));
	rpc_set_port((struct sockaddr *)&rp->c_addr, rpc_get_port(svc_addr(rqstp)));
	rp->c_prot = proto;
	rp->c_vers = vers;
	rp->c_len = rqstp->rq_arg.len;
	rp->c_csum = csum;

	lru_put_end(b, rp);

	/* release any buffer */
	if (rp->c_type == RC_REPLBUFF) {
		drc_mem_usage -= rp->c_replvec.iov_len;
		kfree(rp->c_replvec.iov_base);
		rp->c_replvec.iov_base = NULL;
	}
	rp->c_type = RC_NOCACHE;
 out:
	spin_unlock(&b->cache_lock);
	return rtn;

found_entry:
	nfsdstats.rchits++;
	/* We found a matching entry which is either in progress or done. */
	lru_put_end(b, rp);

	rtn = RC_DROPIT;
	/* Request being processed */
	if (rp->c_state == RC_INPROG)
		goto out;

	/* From the hall of fame of impractical attacks:
	 * Is this a user who tries to snoop on the cache? */
	rtn = RC_DOIT;
	if (!test_bit(RQ_SECURE, &rqstp->rq_flags) && rp->c_secure)
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
	u32		hash;
	struct nfsd_drc_bucket *b;
	int		len;
	size_t		bufsize = 0;

	if (!rp)
		return;

	hash = nfsd_cache_hash(rp->c_xid);
	b = &drc_hashtbl[hash];

	len = resv->iov_len - ((char*)statp - (char*)resv->iov_base);
	len >>= 2;

	/* Don't cache excessive amounts of data and XDR failures */
	if (!statp || len > (256 >> 2)) {
		nfsd_reply_cache_free(b, rp);
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
		bufsize = len << 2;
		cachv->iov_base = kmalloc(bufsize, GFP_KERNEL);
		if (!cachv->iov_base) {
			nfsd_reply_cache_free(b, rp);
			return;
		}
		cachv->iov_len = bufsize;
		memcpy(cachv->iov_base, statp, bufsize);
		break;
	case RC_NOCACHE:
		nfsd_reply_cache_free(b, rp);
		return;
	}
	spin_lock(&b->cache_lock);
	drc_mem_usage += bufsize;
	lru_put_end(b, rp);
	rp->c_secure = test_bit(RQ_SECURE, &rqstp->rq_flags);
	rp->c_type = cachetype;
	rp->c_state = RC_DONE;
	spin_unlock(&b->cache_lock);
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
		printk(KERN_WARNING "nfsd: cached reply too large (%zd).\n",
				data->iov_len);
		return 0;
	}
	memcpy((char*)vec->iov_base + vec->iov_len, data->iov_base, data->iov_len);
	vec->iov_len += data->iov_len;
	return 1;
}

/*
 * Note that fields may be added, removed or reordered in the future. Programs
 * scraping this file for info should test the labels to ensure they're
 * getting the correct field.
 */
static int nfsd_reply_cache_stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "max entries:           %u\n", max_drc_entries);
	seq_printf(m, "num entries:           %u\n",
			atomic_read(&num_drc_entries));
	seq_printf(m, "hash buckets:          %u\n", 1 << maskbits);
	seq_printf(m, "mem usage:             %u\n", drc_mem_usage);
	seq_printf(m, "cache hits:            %u\n", nfsdstats.rchits);
	seq_printf(m, "cache misses:          %u\n", nfsdstats.rcmisses);
	seq_printf(m, "not cached:            %u\n", nfsdstats.rcnocache);
	seq_printf(m, "payload misses:        %u\n", payload_misses);
	seq_printf(m, "longest chain len:     %u\n", longest_chain);
	seq_printf(m, "cachesize at longest:  %u\n", longest_chain_cachesize);
	return 0;
}

int nfsd_reply_cache_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, nfsd_reply_cache_stats_show, NULL);
}
