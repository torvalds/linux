// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "buckets_waiting_for_journal.h"
#include <linux/hash.h>
#include <linux/random.h>

static inline struct bucket_hashed *
bucket_hash(struct buckets_waiting_for_journal_table *t,
	    unsigned hash_seed_idx, u64 dev_bucket)
{
	return t->d + hash_64(dev_bucket ^ t->hash_seeds[hash_seed_idx], t->bits);
}

static void bucket_table_init(struct buckets_waiting_for_journal_table *t, size_t bits)
{
	unsigned i;

	t->bits = bits;
	for (i = 0; i < ARRAY_SIZE(t->hash_seeds); i++)
		get_random_bytes(&t->hash_seeds[i], sizeof(t->hash_seeds[i]));
	memset(t->d, 0, sizeof(t->d[0]) << t->bits);
}

bool bch2_bucket_needs_journal_commit(struct buckets_waiting_for_journal *b,
				      u64 flushed_seq,
				      unsigned dev, u64 bucket)
{
	struct buckets_waiting_for_journal_table *t;
	u64 dev_bucket = (u64) dev << 56 | bucket;
	bool ret = false;
	unsigned i;

	mutex_lock(&b->lock);
	t = b->t;

	for (i = 0; i < ARRAY_SIZE(t->hash_seeds); i++) {
		struct bucket_hashed *h = bucket_hash(t, i, dev_bucket);

		if (h->dev_bucket == dev_bucket) {
			ret = h->journal_seq > flushed_seq;
			break;
		}
	}

	mutex_unlock(&b->lock);

	return ret;
}

static bool bucket_table_insert(struct buckets_waiting_for_journal_table *t,
				struct bucket_hashed *new,
				u64 flushed_seq)
{
	struct bucket_hashed *last_evicted = NULL;
	unsigned tries, i;

	for (tries = 0; tries < 10; tries++) {
		struct bucket_hashed *old, *victim = NULL;

		for (i = 0; i < ARRAY_SIZE(t->hash_seeds); i++) {
			old = bucket_hash(t, i, new->dev_bucket);

			if (old->dev_bucket == new->dev_bucket ||
			    old->journal_seq <= flushed_seq) {
				*old = *new;
				return true;
			}

			if (last_evicted != old)
				victim = old;
		}

		/* hashed to same slot 3 times: */
		if (!victim)
			break;

		/* Failed to find an empty slot: */
		swap(*new, *victim);
		last_evicted = victim;
	}

	return false;
}

int bch2_set_bucket_needs_journal_commit(struct buckets_waiting_for_journal *b,
					 u64 flushed_seq,
					 unsigned dev, u64 bucket,
					 u64 journal_seq)
{
	struct buckets_waiting_for_journal_table *t, *n;
	struct bucket_hashed tmp, new = {
		.dev_bucket	= (u64) dev << 56 | bucket,
		.journal_seq	= journal_seq,
	};
	size_t i, size, new_bits, nr_elements = 1, nr_rehashes = 0, nr_rehashes_this_size = 0;
	int ret = 0;

	mutex_lock(&b->lock);

	if (likely(bucket_table_insert(b->t, &new, flushed_seq)))
		goto out;

	t = b->t;
	size = 1UL << t->bits;
	for (i = 0; i < size; i++)
		nr_elements += t->d[i].journal_seq > flushed_seq;

	new_bits = ilog2(roundup_pow_of_two(nr_elements * 3));
realloc:
	n = kvmalloc(sizeof(*n) + (sizeof(n->d[0]) << new_bits), GFP_KERNEL);
	if (!n) {
		ret = -BCH_ERR_ENOMEM_buckets_waiting_for_journal_set;
		goto out;
	}

retry_rehash:
	if (nr_rehashes_this_size == 3) {
		new_bits++;
		nr_rehashes_this_size = 0;
		kvfree(n);
		goto realloc;
	}

	nr_rehashes++;
	nr_rehashes_this_size++;

	bucket_table_init(n, new_bits);

	tmp = new;
	BUG_ON(!bucket_table_insert(n, &tmp, flushed_seq));

	for (i = 0; i < 1UL << t->bits; i++) {
		if (t->d[i].journal_seq <= flushed_seq)
			continue;

		tmp = t->d[i];
		if (!bucket_table_insert(n, &tmp, flushed_seq))
			goto retry_rehash;
	}

	b->t = n;
	kvfree(t);

	pr_debug("took %zu rehashes, table at %zu/%lu elements",
		 nr_rehashes, nr_elements, 1UL << b->t->bits);
out:
	mutex_unlock(&b->lock);

	return ret;
}

void bch2_fs_buckets_waiting_for_journal_exit(struct bch_fs *c)
{
	struct buckets_waiting_for_journal *b = &c->buckets_waiting_for_journal;

	kvfree(b->t);
}

#define INITIAL_TABLE_BITS		3

int bch2_fs_buckets_waiting_for_journal_init(struct bch_fs *c)
{
	struct buckets_waiting_for_journal *b = &c->buckets_waiting_for_journal;

	mutex_init(&b->lock);

	b->t = kvmalloc(sizeof(*b->t) +
			(sizeof(b->t->d[0]) << INITIAL_TABLE_BITS), GFP_KERNEL);
	if (!b->t)
		return -BCH_ERR_ENOMEM_buckets_waiting_for_journal_init;

	bucket_table_init(b->t, INITIAL_TABLE_BITS);
	return 0;
}
