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
#define TARGET_BUCKET_SIZE	8

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

static struct nfsd_cacherep *
nfsd_cacherep_alloc(struct svc_rqst *rqstp, __wsum csum,
		    struct nfsd_net *nn)
{
	struct nfsd_cacherep *rp;

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

static void nfsd_cacherep_free(struct nfsd_cacherep *rp)
{
	if (rp->c_type == RC_REPLBUFF)
		kfree(rp->c_replvec.iov_base);
	kmem_cache_free(drc_slab, rp);
}

static unsigned long
nfsd_cacherep_dispose(struct list_head *dispose)
{
	struct nfsd_cacherep *rp;
	unsigned long freed = 0;

	while (!list_empty(dispose)) {
		rp = list_first_entry(dispose, struct nfsd_cacherep, c_lru);
		list_del(&rp->c_lru);
		nfsd_cacherep_free(rp);
		freed++;
	}
	return freed;
}

static void
nfsd_cacherep_unlink_locked(struct nfsd_net *nn, struct nfsd_drc_bucket *b,
			    struct nfsd_cacherep *rp)
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
nfsd_reply_cache_free_locked(struct nfsd_drc_bucket *b, struct nfsd_cacherep *rp,
				struct nfsd_net *nn)
{
	nfsd_cacherep_unlink_locked(nn, b, rp);
	nfsd_cacherep_free(rp);
}

static void
nfsd_reply_cache_free(struct nfsd_drc_bucket *b, struct nfsd_cacherep *rp,
			struct nfsd_net *nn)
{
	spin_lock(&b->cache_lock);
	nfsd_cacherep_unlink_locked(nn, b, rp);
	spin_unlock(&b->cache_lock);
	nfsd_cacherep_free(rp);
}

int nfsd_drc_slab_create(void)
{
	drc_slab = KMEM_CACHE(nfsd_cacherep, 0);
	return drc_slab ? 0: -ENOMEM;
}

void nfsd_drc_slab_free(void)
{
	kmem_cache_destroy(drc_slab);
}

int nfsd_reply_cache_init(struct nfsd_net *nn)
{
	unsigned int hashsize;
	unsigned int i;

	nn->max_drc_entries = nfsd_cache_size_limit();
	atomic_set(&nn->num_drc_entries, 0);
	hashsize = nfsd_hashsize(nn->max_drc_entries);
	nn->maskbits = ilog2(hashsize);

	nn->drc_hashtbl = kvzalloc(array_size(hashsize,
				sizeof(*nn->drc_hashtbl)), GFP_KERNEL);
	if (!nn->drc_hashtbl)
		return -ENOMEM;

	nn->nfsd_reply_cache_shrinker = shrinker_alloc(0, "nfsd-reply:%s",
						       nn->nfsd_name);
	if (!nn->nfsd_reply_cache_shrinker)
		goto out_shrinker;

	nn->nfsd_reply_cache_shrinker->scan_objects = nfsd_reply_cache_scan;
	nn->nfsd_reply_cache_shrinker->count_objects = nfsd_reply_cache_count;
	nn->nfsd_reply_cache_shrinker->seeks = 1;
	nn->nfsd_reply_cache_shrinker->private_data = nn;

	shrinker_register(nn->nfsd_reply_cache_shrinker);

	for (i = 0; i < hashsize; i++) {
		INIT_LIST_HEAD(&nn->drc_hashtbl[i].lru_head);
		spin_lock_init(&nn->drc_hashtbl[i].cache_lock);
	}
	nn->drc_hashsize = hashsize;

	return 0;
out_shrinker:
	kvfree(nn->drc_hashtbl);
	printk(KERN_ERR "nfsd: failed to allocate reply cache\n");
	return -ENOMEM;
}

void nfsd_reply_cache_shutdown(struct nfsd_net *nn)
{
	struct nfsd_cacherep *rp;
	unsigned int i;

	shrinker_free(nn->nfsd_reply_cache_shrinker);

	for (i = 0; i < nn->drc_hashsize; i++) {
		struct list_head *head = &nn->drc_hashtbl[i].lru_head;
		while (!list_empty(head)) {
			rp = list_first_entry(head, struct nfsd_cacherep, c_lru);
			nfsd_reply_cache_free_locked(&nn->drc_hashtbl[i],
									rp, nn);
		}
	}

	kvfree(nn->drc_hashtbl);
	nn->drc_hashtbl = NULL;
	nn->drc_hashsize = 0;

}

static void
lru_put_end(struct nfsd_drc_bucket *b, struct nfsd_cacherep *rp)
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

/*
 * Remove and return no more than @max expired entries in bucket @b.
 * If @max is zero, do not limit the number of removed entries.
 */
static void
nfsd_prune_bucket_locked(struct nfsd_net *nn, struct nfsd_drc_bucket *b,
			 unsigned int max, struct list_head *dispose)
{
	unsigned long expiry = jiffies - RC_EXPIRE;
	struct nfsd_cacherep *rp, *tmp;
	unsigned int freed = 0;

	lockdep_assert_held(&b->cache_lock);

	/* The bucket LRU is ordered oldest-first. */
	list_for_each_entry_safe(rp, tmp, &b->lru_head, c_lru) {
		if (atomic_read(&nn->num_drc_entries) <= nn->max_drc_entries &&
		    time_before(expiry, rp->c_timestamp))
			break;

		nfsd_cacherep_unlink_locked(nn, b, rp);
		list_add(&rp->c_lru, dispose);

		if (max && ++freed > max)
			break;
	}
}

/**
 * nfsd_reply_cache_count - count_objects method for the DRC shrinker
 * @shrink: our registered shrinker context
 * @sc: garbage collection parameters
 *
 * Returns the total number of entries in the duplicate reply cache. To
 * keep things simple and quick, this is not the number of expired entries
 * in the cache (ie, the number that would be removed by a call to
 * nfsd_reply_cache_scan).
 */
static unsigned long
nfsd_reply_cache_count(struct shrinker *shrink, struct shrink_control *sc)
{
	struct nfsd_net *nn = shrink->private_data;

	return atomic_read(&nn->num_drc_entries);
}

/**
 * nfsd_reply_cache_scan - scan_objects method for the DRC shrinker
 * @shrink: our registered shrinker context
 * @sc: garbage collection parameters
 *
 * Free expired entries on each bucket's LRU list until we've released
 * nr_to_scan freed objects. Nothing will be released if the cache
 * has not exceeded it's max_drc_entries limit.
 *
 * Returns the number of entries released by this call.
 */
static unsigned long
nfsd_reply_cache_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct nfsd_net *nn = shrink->private_data;
	unsigned long freed = 0;
	LIST_HEAD(dispose);
	unsigned int i;

	for (i = 0; i < nn->drc_hashsize; i++) {
		struct nfsd_drc_bucket *b = &nn->drc_hashtbl[i];

		if (list_empty(&b->lru_head))
			continue;

		spin_lock(&b->cache_lock);
		nfsd_prune_bucket_locked(nn, b, 0, &dispose);
		spin_unlock(&b->cache_lock);

		freed += nfsd_cacherep_dispose(&dispose);
		if (freed > sc->nr_to_scan)
			break;
	}
	return freed;
}

/**
 * nfsd_cache_csum - Checksum incoming NFS Call arguments
 * @buf: buffer containing a whole RPC Call message
 * @start: starting byte of the NFS Call header
 * @remaining: size of the NFS Call header, in bytes
 *
 * Compute a weak checksum of the leading bytes of an NFS procedure
 * call header to help verify that a retransmitted Call matches an
 * entry in the duplicate reply cache.
 *
 * To avoid assumptions about how the RPC message is laid out in
 * @buf and what else it might contain (eg, a GSS MIC suffix), the
 * caller passes us the exact location and length of the NFS Call
 * header.
 *
 * Returns a 32-bit checksum value, as defined in RFC 793.
 */
static __wsum nfsd_cache_csum(struct xdr_buf *buf, unsigned int start,
			      unsigned int remaining)
{
	unsigned int base, len;
	struct xdr_buf subbuf;
	__wsum csum = 0;
	void *p;
	int idx;

	if (remaining > RC_CSUMLEN)
		remaining = RC_CSUMLEN;
	if (xdr_buf_subsegment(buf, &subbuf, start, remaining))
		return csum;

	/* rq_arg.head first */
	if (subbuf.head[0].iov_len) {
		len = min_t(unsigned int, subbuf.head[0].iov_len, remaining);
		csum = csum_partial(subbuf.head[0].iov_base, len, csum);
		remaining -= len;
	}

	/* Continue into page array */
	idx = subbuf.page_base / PAGE_SIZE;
	base = subbuf.page_base & ~PAGE_MASK;
	while (remaining) {
		p = page_address(subbuf.pages[idx]) + base;
		len = min_t(unsigned int, PAGE_SIZE - base, remaining);
		csum = csum_partial(p, len, csum);
		remaining -= len;
		base = 0;
		++idx;
	}
	return csum;
}

static int
nfsd_cache_key_cmp(const struct nfsd_cacherep *key,
		   const struct nfsd_cacherep *rp, struct nfsd_net *nn)
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
static struct nfsd_cacherep *
nfsd_cache_insert(struct nfsd_drc_bucket *b, struct nfsd_cacherep *key,
			struct nfsd_net *nn)
{
	struct nfsd_cacherep	*rp, *ret = key;
	struct rb_node		**p = &b->rb_head.rb_node,
				*parent = NULL;
	unsigned int		entries = 0;
	int cmp;

	while (*p != NULL) {
		++entries;
		parent = *p;
		rp = rb_entry(parent, struct nfsd_cacherep, c_node);

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
	return ret;
}

/**
 * nfsd_cache_lookup - Find an entry in the duplicate reply cache
 * @rqstp: Incoming Call to find
 * @start: starting byte in @rqstp->rq_arg of the NFS Call header
 * @len: size of the NFS Call header, in bytes
 * @cacherep: OUT: DRC entry for this request
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
int nfsd_cache_lookup(struct svc_rqst *rqstp, unsigned int start,
		      unsigned int len, struct nfsd_cacherep **cacherep)
{
	struct nfsd_net		*nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	struct nfsd_cacherep	*rp, *found;
	__wsum			csum;
	struct nfsd_drc_bucket	*b;
	int type = rqstp->rq_cachetype;
	LIST_HEAD(dispose);
	int rtn = RC_DOIT;

	if (type == RC_NOCACHE) {
		nfsd_stats_rc_nocache_inc(nn);
		goto out;
	}

	csum = nfsd_cache_csum(&rqstp->rq_arg, start, len);

	/*
	 * Since the common case is a cache miss followed by an insert,
	 * preallocate an entry.
	 */
	rp = nfsd_cacherep_alloc(rqstp, csum, nn);
	if (!rp)
		goto out;

	b = nfsd_cache_bucket_find(rqstp->rq_xid, nn);
	spin_lock(&b->cache_lock);
	found = nfsd_cache_insert(b, rp, nn);
	if (found != rp)
		goto found_entry;
	*cacherep = rp;
	rp->c_state = RC_INPROG;
	nfsd_prune_bucket_locked(nn, b, 3, &dispose);
	spin_unlock(&b->cache_lock);

	nfsd_cacherep_dispose(&dispose);

	nfsd_stats_rc_misses_inc(nn);
	atomic_inc(&nn->num_drc_entries);
	nfsd_stats_drc_mem_usage_add(nn, sizeof(*rp));
	goto out;

found_entry:
	/* We found a matching entry which is either in progress or done. */
	nfsd_reply_cache_free_locked(NULL, rp, nn);
	nfsd_stats_rc_hits_inc(nn);
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
out_unlock:
	spin_unlock(&b->cache_lock);
out:
	return rtn;
}

/**
 * nfsd_cache_update - Update an entry in the duplicate reply cache.
 * @rqstp: svc_rqst with a finished Reply
 * @rp: IN: DRC entry for this request
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
void nfsd_cache_update(struct svc_rqst *rqstp, struct nfsd_cacherep *rp,
		       int cachetype, __be32 *statp)
{
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
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

static int
nfsd_cache_append(struct svc_rqst *rqstp, struct kvec *data)
{
	__be32 *p;

	p = xdr_reserve_space(&rqstp->rq_res_stream, data->iov_len);
	if (unlikely(!p))
		return false;
	memcpy(p, data->iov_base, data->iov_len);
	xdr_commit_encode(&rqstp->rq_res_stream);
	return true;
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
		   percpu_counter_sum_positive(&nn->counter[NFSD_STATS_DRC_MEM_USAGE]));
	seq_printf(m, "cache hits:            %lld\n",
		   percpu_counter_sum_positive(&nn->counter[NFSD_STATS_RC_HITS]));
	seq_printf(m, "cache misses:          %lld\n",
		   percpu_counter_sum_positive(&nn->counter[NFSD_STATS_RC_MISSES]));
	seq_printf(m, "not cached:            %lld\n",
		   percpu_counter_sum_positive(&nn->counter[NFSD_STATS_RC_NOCACHE]));
	seq_printf(m, "payload misses:        %lld\n",
		   percpu_counter_sum_positive(&nn->counter[NFSD_STATS_PAYLOAD_MISSES]));
	seq_printf(m, "longest chain len:     %u\n", nn->longest_chain);
	seq_printf(m, "cachesize at longest:  %u\n", nn->longest_chain_cachesize);
	return 0;
}
