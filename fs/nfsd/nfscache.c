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

#include <linux/sunrpc/svc_xprt.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sunrpc/addr.h>
#include <linux/highmem.h>
#include <linux/log2.h>
#include <linux/hash.h>
#include <net/checksum.h>

#include "nfsd.h"
#include "cache.h"
#include "trace.h"

/*
 * We use this value to determine the number of hash buckets from the max
 * cache size, the idea being that when the cache is at its maximum number
 * of entries, then this should be the average number of entries per bucket.
 */
#define TARGET_BUCKET_SIZE	64

struct nfsd_drc_bucket {
	struct rb_root rb_head;
	struct list_head lru_head;
	spinlock_t cache_lock;
};

static struct kmem_cache	*drc_slab;

static int	nfsd_cache_append(struct svc_rqst *rqstp, struct kvec *vec);
static unsigned long nfsd_reply_cache_count(struct shrinker *shrink,
					    struct shrink_control *sc);
static unsigned long nfsd_reply_cache_scan(struct shrinker *shrink,
					   struct shrink_control *sc);

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
 *
 * XXX: these limits are per-container, so memory used will increase
 * linearly with number of containers.  Maybe that's OK.
 */
static unsigned int
nfsd_cache_size_limit(void)
{
	unsigned int limit;
	unsigned long low_pages = totalram_pages() - totalhigh_pages();

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

static struct svc_cacherep *
nfsd_cacherep_alloc(struct svc_rqst *rqstp, __wsum csum,
		    struct nfsd_net *nn)
{
	struct svc_cacherep	*rp;

	rp = kmem_cache_alloc(drc_slab, GFP_KERNEL);
	if (rp) {
		rp->c_state = RC_UNUSED;
		rp->c_type = RC_NOCACHE;
		RB_CLEAR_NODE(&rp->c_node);
		INIT_LIST_HEAD(&rp->c_lru);

		memset(&rp->c_key, 0, sizeof(rp->c_key));
		rp->c_key.k_xid = rqstp->rq_xid;
		rp->c_key.k_proc = rqstp->rq_proc;
		rpc_copy_addr((struct sockaddr *)&rp->c_key.k_addr, svc_addr(rqstp));
		rpc_set_port((struct sockaddr *)&rp->c_key.k_addr, rpc_get_port(svc_addr(rqstp)));
		rp->c_key.k_prot = rqstp->rq_prot;
		rp->c_key.k_vers = rqstp->rq_vers;
		rp->c_key.k_len = rqstp->rq_arg.len;
		rp->c_key.k_csum = csum;
	}
	return rp;
}

static void nfsd_cacherep_free(struct svc_cacherep *rp)
{
	if (rp->c_type == RC_REPLBUFF)
		kfree(rp->c_replvec.iov_base);
	kmem_cache_free(drc_slab, rp);
}

static void
nfsd_cacherep_unlink_locked(struct nfsd_net *nn, struct nfsd_drc_bucket *b,
			    struct svc_cacherep *rp)
{
	if (rp->c_type == RC_REPLBUFF && rp->c_replvec.iov_base)
		nfsd_stats_drc_mem_usage_sub(nn, rp->c_replvec.iov_len);
	if (rp->c_state != RC_UNUSED) {
		rb_erase(&rp->c_node, &b->rb_head);
		list_del(&rp->c_lru);
		atomic_dec(&nn->num_drc_entries);
		nfsd_stats_drc_mem_usage_sub(nn, sizeof(*rp));
	}
}

static void
nfsd_reply_cache_free_locked(struct nfsd_drc_bucket *b, struct svc_cacherep *rp,
				struct nfsd_net *nn)
{
	nfsd_cacherep_unlink_locked(nn, b, rp);
	nfsd_cacherep_free(rp);
}

static void
nfsd_reply_cache_free(struct nfsd_drc_bucket *b, struct svc_cacherep *rp,
			struct nfsd_net *nn)
{
	spin_lock(&b->cache_lock);
	nfsd_cacherep_unlink_locked(nn, b, rp);
	spin_unlock(&b->cache_lock);
	nfsd_cacherep_free(rp);
}

int nfsd_drc_slab_create(void)
{
	drc_slab = kmem_cache_create("nfsd_drc",
				sizeof(struct svc_cacherep), 0, 0, NULL);
	return drc_slab ? 0: -ENOMEM;
}

void nfsd_drc_slab_free(void)
{
	kmem_cache_destroy(drc_slab);
}

/**
 * nfsd_net_reply_cache_init - per net namespace reply cache set-up
 * @nn: nfsd_net being initialized
 *
 * Returns zero on succes; otherwise a negative errno is returned.
 */
int nfsd_net_reply_cache_init(struct nfsd_net *nn)
{
	return nfsd_percpu_counters_init(nn->counter, NFSD_NET_COUNTERS_NUM);
}

/**
 * nfsd_net_reply_cache_destroy - per net namespace reply cache tear-down
 * @nn: nfsd_net being freed
 *
 */
void nfsd_net_reply_cache_destroy(struct nfsd_net *nn)
{
	nfsd_percpu_counters_destroy(nn->counter, NFSD_NET_COUNTERS_NUM);
}

int nfsd_reply_cache_init(struct nfsd_net *nn)
{
	unsigned int hashsize;
	unsigned int i;
	int status = 0;

	nn->max_drc_entries = nfsd_cache_size_limit();
	atomic_set(&nn->num_drc_entries, 0);
	hashsize = nfsd_hashsize(nn->max_drc_entries);
	nn->maskbits = ilog2(hashsize);

	nn->nfsd_reply_cache_shrinker.scan_objects = nfsd_reply_cache_scan;
	nn->nfsd_reply_cache_shrinker.count_objects = nfsd_reply_cache_count;
	nn->nfsd_reply_cache_shrinker.seeks = 1;
	status = register_shrinker(&nn->nfsd_reply_cache_shrinker,
				   "nfsd-reply:%s", nn->nfsd_name);
	if (status)
		return status;

	nn->drc_hashtbl = kvzalloc(array_size(hashsize,
				sizeof(*nn->drc_hashtbl)), GFP_KERNEL);
	if (!nn->drc_hashtbl)
		goto out_shrinker;

	for (i = 0; i < hashsize; i++) {
		INIT_LIST_HEAD(&nn->drc_hashtbl[i].lru_head);
		spin_lock_init(&nn->drc_hashtbl[i].cache_lock);
	}
	nn->drc_hashsize = hashsize;

	return 0;
out_shrinker:
	unregister_shrinker(&nn->nfsd_reply_cache_shrinker);
	printk(KERN_ERR "nfsd: failed to allocate reply cache\n");
	return -ENOMEM;
}

void nfsd_reply_cache_shutdown(struct nfsd_net *nn)
{
	struct svc_cacherep	*rp;
	unsigned int i;

	unregister_shrinker(&nn->nfsd_reply_cache_shrinker);

	for (i = 0; i < nn->drc_hashsize; i++) {
		struct list_head *head = &nn->drc_hashtbl[i].lru_head;
		while (!list_empty(head)) {
			rp = list_first_entry(head, struct svc_cacherep, c_lru);
			nfsd_reply_cache_free_locked(&nn->drc_hashtbl[i],
									rp, nn);
		}
	}

	kvfree(nn->drc_hashtbl);
	nn->drc_hashtbl = NULL;
	nn->drc_hashsize = 0;

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

static noinline struct nfsd_drc_bucket *
nfsd_cache_bucket_find(__be32 xid, struct nfsd_net *nn)
{
	unsigned int hash = hash_32((__force u32)xid, nn->maskbits);

	return &nn->drc_hashtbl[hash];
}

static long prune_bucket(struct nfsd_drc_bucket *b, struct nfsd_net *nn,
			 unsigned int max)
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
		if (atomic_read(&nn->num_drc_entries) <= nn->max_drc_entries &&
		    time_before(jiffies, rp->c_timestamp + RC_EXPIRE))
			break;
		nfsd_reply_cache_free_locked(b, rp, nn);
		if (max && freed++ > max)
			break;
	}
	return freed;
}

static long nfsd_prune_bucket(struct nfsd_drc_bucket *b, struct nfsd_net *nn)
{
	return prune_bucket(b, nn, 3);
}

/*
 * Walk the LRU list and prune off entries that are older than RC_EXPIRE.
 * Also prune the oldest ones when the total exceeds the max number of entries.
 */
static long
prune_cache_entries(struct nfsd_net *nn)
{
	unsigned int i;
	long freed = 0;

	for (i = 0; i < nn->drc_hashsize; i++) {
		struct nfsd_drc_bucket *b = &nn->drc_hashtbl[i];

		if (list_empty(&b->lru_head))
			continue;
		spin_lock(&b->cache_lock);
		freed += prune_bucket(b, nn, 0);
		spin_unlock(&b->cache_lock);
	}
	return freed;
}

static unsigned long
nfsd_reply_cache_count(struct shrinker *shrink, struct shrink_control *sc)
{
	struct nfsd_net *nn = container_of(shrink,
				struct nfsd_net, nfsd_reply_cache_shrinker);

	return atomic_read(&nn->num_drc_entries);
}

static unsigned long
nfsd_reply_cache_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct nfsd_net *nn = container_of(shrink,
				struct nfsd_net, nfsd_reply_cache_shrinker);

	return prune_cache_entries(nn);
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

static int
nfsd_cache_key_cmp(const struct svc_cacherep *key,
			const struct svc_cacherep *rp, struct nfsd_net *nn)
{
	if (key->c_key.k_xid == rp->c_key.k_xid &&
	    key->c_key.k_csum != rp->c_key.k_csum) {
		nfsd_stats_payload_misses_inc(nn);
		trace_nfsd_drc_mismatch(nn, key, rp);
	}

	return memcmp(&key->c_key, &rp->c_key, sizeof(key->c_key));
}

/*
 * Search the request hash for an entry that matches the given rqstp.
 * Must be called with cache_lock held. Returns the found entry or
 * inserts an empty key on failure.
 */
static struct svc_cacherep *
nfsd_cache_insert(struct nfsd_drc_bucket *b, struct svc_cacherep *key,
			struct nfsd_net *nn)
{
	struct svc_cacherep	*rp, *ret = key;
	struct rb_node		**p = &b->rb_head.rb_node,
				*parent = NULL;
	unsigned int		entries = 0;
	int cmp;

	while (*p != NULL) {
		++entries;
		parent = *p;
		rp = rb_entry(parent, struct svc_cacherep, c_node);

		cmp = nfsd_cache_key_cmp(key, rp, nn);
		if (cmp < 0)
			p = &parent->rb_left;
		else if (cmp > 0)
			p = &parent->rb_right;
		else {
			ret = rp;
			goto out;
		}
	}
	rb_link_node(&key->c_node, parent, p);
	rb_insert_color(&key->c_node, &b->rb_head);
out:
	/* tally hash chain length stats */
	if (entries > nn->longest_chain) {
		nn->longest_chain = entries;
		nn->longest_chain_cachesize = atomic_read(&nn->num_drc_entries);
	} else if (entries == nn->longest_chain) {
		/* prefer to keep the smallest cachesize possible here */
		nn->longest_chain_cachesize = min_t(unsigned int,
				nn->longest_chain_cachesize,
				atomic_read(&nn->num_drc_entries));
	}

	lru_put_end(b, ret);
	return ret;
}

/**
 * nfsd_cache_lookup - Find an entry in the duplicate reply cache
 * @rqstp: Incoming Call to find
 *
 * Try to find an entry matching the current call in the cache. When none
 * is found, we try to grab the oldest expired entry off the LRU list. If
 * a suitable one isn't there, then drop the cache_lock and allocate a
 * new one, then search again in case one got inserted while this thread
 * didn't hold the lock.
 *
 * Return values:
 *   %RC_DOIT: Process the request normally
 *   %RC_REPLY: Reply from cache
 *   %RC_DROPIT: Do not process the request further
 */
int nfsd_cache_lookup(struct svc_rqst *rqstp)
{
	struct nfsd_net		*nn;
	struct svc_cacherep	*rp, *found;
	__wsum			csum;
	struct nfsd_drc_bucket	*b;
	int type = rqstp->rq_cachetype;
	int rtn = RC_DOIT;

	rqstp->rq_cacherep = NULL;
	if (type == RC_NOCACHE) {
		nfsd_stats_rc_nocache_inc();
		goto out;
	}

	csum = nfsd_cache_csum(rqstp);

	/*
	 * Since the common case is a cache miss followed by an insert,
	 * preallocate an entry.
	 */
	nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	rp = nfsd_cacherep_alloc(rqstp, csum, nn);
	if (!rp)
		goto out;

	b = nfsd_cache_bucket_find(rqstp->rq_xid, nn);
	spin_lock(&b->cache_lock);
	found = nfsd_cache_insert(b, rp, nn);
	if (found != rp)
		goto found_entry;

	nfsd_stats_rc_misses_inc();
	rqstp->rq_cacherep = rp;
	rp->c_state = RC_INPROG;

	atomic_inc(&nn->num_drc_entries);
	nfsd_stats_drc_mem_usage_add(nn, sizeof(*rp));

	nfsd_prune_bucket(b, nn);

out_unlock:
	spin_unlock(&b->cache_lock);
out:
	return rtn;

found_entry:
	/* We found a matching entry which is either in progress or done. */
	nfsd_reply_cache_free_locked(NULL, rp, nn);
	nfsd_stats_rc_hits_inc();
	rtn = RC_DROPIT;
	rp = found;

	/* Request being processed */
	if (rp->c_state == RC_INPROG)
		goto out_trace;

	/* From the hall of fame of impractical attacks:
	 * Is this a user who tries to snoop on the cache? */
	rtn = RC_DOIT;
	if (!test_bit(RQ_SECURE, &rqstp->rq_flags) && rp->c_secure)
		goto out_trace;

	/* Compose RPC reply header */
	switch (rp->c_type) {
	case RC_NOCACHE:
		break;
	case RC_REPLSTAT:
		xdr_stream_encode_be32(&rqstp->rq_res_stream, rp->c_replstat);
		rtn = RC_REPLY;
		break;
	case RC_REPLBUFF:
		if (!nfsd_cache_append(rqstp, &rp->c_replvec))
			goto out_unlock; /* should not happen */
		rtn = RC_REPLY;
		break;
	default:
		WARN_ONCE(1, "nfsd: bad repcache type %d\n", rp->c_type);
	}

out_trace:
	trace_nfsd_drc_found(nn, rqstp, rtn);
	goto out_unlock;
}

/**
 * nfsd_cache_update - Update an entry in the duplicate reply cache.
 * @rqstp: svc_rqst with a finished Reply
 * @cachetype: which cache to update
 * @statp: pointer to Reply's NFS status code, or NULL
 *
 * This is called from nfsd_dispatch when the procedure has been
 * executed and the complete reply is in rqstp->rq_res.
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
void nfsd_cache_update(struct svc_rqst *rqstp, int cachetype, __be32 *statp)
{
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	struct svc_cacherep *rp = rqstp->rq_cacherep;
	struct kvec	*resv = &rqstp->rq_res.head[0], *cachv;
	struct nfsd_drc_bucket *b;
	int		len;
	size_t		bufsize = 0;

	if (!rp)
		return;

	b = nfsd_cache_bucket_find(rp->c_key.k_xid, nn);

	len = resv->iov_len - ((char*)statp - (char*)resv->iov_base);
	len >>= 2;

	/* Don't cache excessive amounts of data and XDR failures */
	if (!statp || len > (256 >> 2)) {
		nfsd_reply_cache_free(b, rp, nn);
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
			nfsd_reply_cache_free(b, rp, nn);
			return;
		}
		cachv->iov_len = bufsize;
		memcpy(cachv->iov_base, statp, bufsize);
		break;
	case RC_NOCACHE:
		nfsd_reply_cache_free(b, rp, nn);
		return;
	}
	spin_lock(&b->cache_lock);
	nfsd_stats_drc_mem_usage_add(nn, bufsize);
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
int nfsd_reply_cache_stats_show(struct seq_file *m, void *v)
{
	struct nfsd_net *nn = net_generic(file_inode(m->file)->i_sb->s_fs_info,
					  nfsd_net_id);

	seq_printf(m, "max entries:           %u\n", nn->max_drc_entries);
	seq_printf(m, "num entries:           %u\n",
		   atomic_read(&nn->num_drc_entries));
	seq_printf(m, "hash buckets:          %u\n", 1 << nn->maskbits);
	seq_printf(m, "mem usage:             %lld\n",
		   percpu_counter_sum_positive(&nn->counter[NFSD_NET_DRC_MEM_USAGE]));
	seq_printf(m, "cache hits:            %lld\n",
		   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_RC_HITS]));
	seq_printf(m, "cache misses:          %lld\n",
		   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_RC_MISSES]));
	seq_printf(m, "not cached:            %lld\n",
		   percpu_counter_sum_positive(&nfsdstats.counter[NFSD_STATS_RC_NOCACHE]));
	seq_printf(m, "payload misses:        %lld\n",
		   percpu_counter_sum_positive(&nn->counter[NFSD_NET_PAYLOAD_MISSES]));
	seq_printf(m, "longest chain len:     %u\n", nn->longest_chain);
	seq_printf(m, "cachesize at longest:  %u\n", nn->longest_chain_cachesize);
	return 0;
}
