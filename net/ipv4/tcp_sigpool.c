// SPDX-License-Identifier: GPL-2.0-or-later

#include <crypto/hash.h>
#include <linux/cpu.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/workqueue.h>
#include <net/tcp.h>

static size_t __scratch_size;
static DEFINE_PER_CPU(void __rcu *, sigpool_scratch);

struct sigpool_entry {
	struct crypto_ahash	*hash;
	const char		*alg;
	struct kref		kref;
	uint16_t		needs_key:1,
				reserved:15;
};

#define CPOOL_SIZE (PAGE_SIZE / sizeof(struct sigpool_entry))
static struct sigpool_entry cpool[CPOOL_SIZE];
static unsigned int cpool_populated;
static DEFINE_MUTEX(cpool_mutex);

/* Slow-path */
struct scratches_to_free {
	struct rcu_head rcu;
	unsigned int cnt;
	void *scratches[];
};

static void free_old_scratches(struct rcu_head *head)
{
	struct scratches_to_free *stf;

	stf = container_of(head, struct scratches_to_free, rcu);
	while (stf->cnt--)
		kfree(stf->scratches[stf->cnt]);
	kfree(stf);
}

/**
 * sigpool_reserve_scratch - re-allocates scratch buffer, slow-path
 * @size: request size for the scratch/temp buffer
 */
static int sigpool_reserve_scratch(size_t size)
{
	struct scratches_to_free *stf;
	size_t stf_sz = struct_size(stf, scratches, num_possible_cpus());
	int cpu, err = 0;

	lockdep_assert_held(&cpool_mutex);
	if (__scratch_size >= size)
		return 0;

	stf = kmalloc(stf_sz, GFP_KERNEL);
	if (!stf)
		return -ENOMEM;
	stf->cnt = 0;

	size = max(size, __scratch_size);
	cpus_read_lock();
	for_each_possible_cpu(cpu) {
		void *scratch, *old_scratch;

		scratch = kmalloc_node(size, GFP_KERNEL, cpu_to_node(cpu));
		if (!scratch) {
			err = -ENOMEM;
			break;
		}

		old_scratch = rcu_replace_pointer(per_cpu(sigpool_scratch, cpu),
					scratch, lockdep_is_held(&cpool_mutex));
		if (!cpu_online(cpu) || !old_scratch) {
			kfree(old_scratch);
			continue;
		}
		stf->scratches[stf->cnt++] = old_scratch;
	}
	cpus_read_unlock();
	if (!err)
		__scratch_size = size;

	call_rcu(&stf->rcu, free_old_scratches);
	return err;
}

static void sigpool_scratch_free(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		kfree(rcu_replace_pointer(per_cpu(sigpool_scratch, cpu),
					  NULL, lockdep_is_held(&cpool_mutex)));
	__scratch_size = 0;
}

static int __cpool_try_clone(struct crypto_ahash *hash)
{
	struct crypto_ahash *tmp;

	tmp = crypto_clone_ahash(hash);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	crypto_free_ahash(tmp);
	return 0;
}

static int __cpool_alloc_ahash(struct sigpool_entry *e, const char *alg)
{
	struct crypto_ahash *cpu0_hash;
	int ret;

	e->alg = kstrdup(alg, GFP_KERNEL);
	if (!e->alg)
		return -ENOMEM;

	cpu0_hash = crypto_alloc_ahash(alg, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(cpu0_hash)) {
		ret = PTR_ERR(cpu0_hash);
		goto out_free_alg;
	}

	e->needs_key = crypto_ahash_get_flags(cpu0_hash) & CRYPTO_TFM_NEED_KEY;

	ret = __cpool_try_clone(cpu0_hash);
	if (ret)
		goto out_free_cpu0_hash;
	e->hash = cpu0_hash;
	kref_init(&e->kref);
	return 0;

out_free_cpu0_hash:
	crypto_free_ahash(cpu0_hash);
out_free_alg:
	kfree(e->alg);
	e->alg = NULL;
	return ret;
}

/**
 * tcp_sigpool_alloc_ahash - allocates pool for ahash requests
 * @alg: name of async hash algorithm
 * @scratch_size: reserve a tcp_sigpool::scratch buffer of this size
 */
int tcp_sigpool_alloc_ahash(const char *alg, size_t scratch_size)
{
	int i, ret;

	/* slow-path */
	mutex_lock(&cpool_mutex);
	ret = sigpool_reserve_scratch(scratch_size);
	if (ret)
		goto out;
	for (i = 0; i < cpool_populated; i++) {
		if (!cpool[i].alg)
			continue;
		if (strcmp(cpool[i].alg, alg))
			continue;

		/* pairs with tcp_sigpool_release() */
		if (!kref_get_unless_zero(&cpool[i].kref))
			kref_init(&cpool[i].kref);
		ret = i;
		goto out;
	}

	for (i = 0; i < cpool_populated; i++) {
		if (!cpool[i].alg)
			break;
	}
	if (i >= CPOOL_SIZE) {
		ret = -ENOSPC;
		goto out;
	}

	ret = __cpool_alloc_ahash(&cpool[i], alg);
	if (!ret) {
		ret = i;
		if (i == cpool_populated)
			cpool_populated++;
	}
out:
	mutex_unlock(&cpool_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(tcp_sigpool_alloc_ahash);

static void __cpool_free_entry(struct sigpool_entry *e)
{
	crypto_free_ahash(e->hash);
	kfree(e->alg);
	memset(e, 0, sizeof(*e));
}

static void cpool_cleanup_work_cb(struct work_struct *work)
{
	bool free_scratch = true;
	unsigned int i;

	mutex_lock(&cpool_mutex);
	for (i = 0; i < cpool_populated; i++) {
		if (kref_read(&cpool[i].kref) > 0) {
			free_scratch = false;
			continue;
		}
		if (!cpool[i].alg)
			continue;
		__cpool_free_entry(&cpool[i]);
	}
	if (free_scratch)
		sigpool_scratch_free();
	mutex_unlock(&cpool_mutex);
}

static DECLARE_WORK(cpool_cleanup_work, cpool_cleanup_work_cb);
static void cpool_schedule_cleanup(struct kref *kref)
{
	schedule_work(&cpool_cleanup_work);
}

/**
 * tcp_sigpool_release - decreases number of users for a pool. If it was
 * the last user of the pool, releases any memory that was consumed.
 * @id: tcp_sigpool that was previously allocated by tcp_sigpool_alloc_ahash()
 */
void tcp_sigpool_release(unsigned int id)
{
	if (WARN_ON_ONCE(id >= cpool_populated || !cpool[id].alg))
		return;

	/* slow-path */
	kref_put(&cpool[id].kref, cpool_schedule_cleanup);
}
EXPORT_SYMBOL_GPL(tcp_sigpool_release);

/**
 * tcp_sigpool_get - increases number of users (refcounter) for a pool
 * @id: tcp_sigpool that was previously allocated by tcp_sigpool_alloc_ahash()
 */
void tcp_sigpool_get(unsigned int id)
{
	if (WARN_ON_ONCE(id >= cpool_populated || !cpool[id].alg))
		return;
	kref_get(&cpool[id].kref);
}
EXPORT_SYMBOL_GPL(tcp_sigpool_get);

int tcp_sigpool_start(unsigned int id, struct tcp_sigpool *c) __cond_acquires(RCU_BH)
{
	struct crypto_ahash *hash;

	rcu_read_lock_bh();
	if (WARN_ON_ONCE(id >= cpool_populated || !cpool[id].alg)) {
		rcu_read_unlock_bh();
		return -EINVAL;
	}

	hash = crypto_clone_ahash(cpool[id].hash);
	if (IS_ERR(hash)) {
		rcu_read_unlock_bh();
		return PTR_ERR(hash);
	}

	c->req = ahash_request_alloc(hash, GFP_ATOMIC);
	if (!c->req) {
		crypto_free_ahash(hash);
		rcu_read_unlock_bh();
		return -ENOMEM;
	}
	ahash_request_set_callback(c->req, 0, NULL, NULL);

	/* Pairs with tcp_sigpool_reserve_scratch(), scratch area is
	 * valid (allocated) until tcp_sigpool_end().
	 */
	c->scratch = rcu_dereference_bh(*this_cpu_ptr(&sigpool_scratch));
	return 0;
}
EXPORT_SYMBOL_GPL(tcp_sigpool_start);

void tcp_sigpool_end(struct tcp_sigpool *c) __releases(RCU_BH)
{
	struct crypto_ahash *hash = crypto_ahash_reqtfm(c->req);

	rcu_read_unlock_bh();
	ahash_request_free(c->req);
	crypto_free_ahash(hash);
}
EXPORT_SYMBOL_GPL(tcp_sigpool_end);

/**
 * tcp_sigpool_algo - return algorithm of tcp_sigpool
 * @id: tcp_sigpool that was previously allocated by tcp_sigpool_alloc_ahash()
 * @buf: buffer to return name of algorithm
 * @buf_len: size of @buf
 */
size_t tcp_sigpool_algo(unsigned int id, char *buf, size_t buf_len)
{
	if (WARN_ON_ONCE(id >= cpool_populated || !cpool[id].alg))
		return -EINVAL;

	return strscpy(buf, cpool[id].alg, buf_len);
}
EXPORT_SYMBOL_GPL(tcp_sigpool_algo);

/**
 * tcp_sigpool_hash_skb_data - hash data in skb with initialized tcp_sigpool
 * @hp: tcp_sigpool pointer
 * @skb: buffer to add sign for
 * @header_len: TCP header length for this segment
 */
int tcp_sigpool_hash_skb_data(struct tcp_sigpool *hp,
			      const struct sk_buff *skb,
			      unsigned int header_len)
{
	const unsigned int head_data_len = skb_headlen(skb) > header_len ?
					   skb_headlen(skb) - header_len : 0;
	const struct skb_shared_info *shi = skb_shinfo(skb);
	const struct tcphdr *tp = tcp_hdr(skb);
	struct ahash_request *req = hp->req;
	struct sk_buff *frag_iter;
	struct scatterlist sg;
	unsigned int i;

	sg_init_table(&sg, 1);

	sg_set_buf(&sg, ((u8 *)tp) + header_len, head_data_len);
	ahash_request_set_crypt(req, &sg, NULL, head_data_len);
	if (crypto_ahash_update(req))
		return 1;

	for (i = 0; i < shi->nr_frags; ++i) {
		const skb_frag_t *f = &shi->frags[i];
		unsigned int offset = skb_frag_off(f);
		struct page *page;

		page = skb_frag_page(f) + (offset >> PAGE_SHIFT);
		sg_set_page(&sg, page, skb_frag_size(f), offset_in_page(offset));
		ahash_request_set_crypt(req, &sg, NULL, skb_frag_size(f));
		if (crypto_ahash_update(req))
			return 1;
	}

	skb_walk_frags(skb, frag_iter)
		if (tcp_sigpool_hash_skb_data(hp, frag_iter, 0))
			return 1;

	return 0;
}
EXPORT_SYMBOL(tcp_sigpool_hash_skb_data);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Per-CPU pool of crypto requests");
