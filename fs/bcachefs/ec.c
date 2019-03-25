// SPDX-License-Identifier: GPL-2.0

/* erasure coding */

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bset.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "buckets.h"
#include "disk_groups.h"
#include "ec.h"
#include "error.h"
#include "io.h"
#include "journal_io.h"
#include "keylist.h"
#include "super-io.h"
#include "util.h"

#include <linux/sort.h>

#ifdef __KERNEL__

#include <linux/raid/pq.h>
#include <linux/raid/xor.h>

static void raid5_recov(unsigned disks, unsigned failed_idx,
			size_t size, void **data)
{
	unsigned i = 2, nr;

	BUG_ON(failed_idx >= disks);

	swap(data[0], data[failed_idx]);
	memcpy(data[0], data[1], size);

	while (i < disks) {
		nr = min_t(unsigned, disks - i, MAX_XOR_BLOCKS);
		xor_blocks(nr, size, data[0], data + i);
		i += nr;
	}

	swap(data[0], data[failed_idx]);
}

static void raid_gen(int nd, int np, size_t size, void **v)
{
	if (np >= 1)
		raid5_recov(nd + np, nd, size, v);
	if (np >= 2)
		raid6_call.gen_syndrome(nd + np, size, v);
	BUG_ON(np > 2);
}

static void raid_rec(int nr, int *ir, int nd, int np, size_t size, void **v)
{
	switch (nr) {
	case 0:
		break;
	case 1:
		if (ir[0] < nd + 1)
			raid5_recov(nd + 1, ir[0], size, v);
		else
			raid6_call.gen_syndrome(nd + np, size, v);
		break;
	case 2:
		if (ir[1] < nd) {
			/* data+data failure. */
			raid6_2data_recov(nd + np, size, ir[0], ir[1], v);
		} else if (ir[0] < nd) {
			/* data + p/q failure */

			if (ir[1] == nd) /* data + p failure */
				raid6_datap_recov(nd + np, size, ir[0], v);
			else { /* data + q failure */
				raid5_recov(nd + 1, ir[0], size, v);
				raid6_call.gen_syndrome(nd + np, size, v);
			}
		} else {
			raid_gen(nd, np, size, v);
		}
		break;
	default:
		BUG();
	}
}

#else

#include <raid/raid.h>

#endif

struct ec_bio {
	struct bch_dev		*ca;
	struct ec_stripe_buf	*buf;
	size_t			idx;
	struct bio		bio;
};

/* Stripes btree keys: */

const char *bch2_stripe_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	const struct bch_stripe *s = bkey_s_c_to_stripe(k).v;

	if (k.k->p.inode)
		return "invalid stripe key";

	if (bkey_val_bytes(k.k) < sizeof(*s))
		return "incorrect value size";

	if (bkey_val_bytes(k.k) < sizeof(*s) ||
	    bkey_val_u64s(k.k) < stripe_val_u64s(s))
		return "incorrect value size";

	return NULL;
}

void bch2_stripe_to_text(struct printbuf *out, struct bch_fs *c,
			 struct bkey_s_c k)
{
	const struct bch_stripe *s = bkey_s_c_to_stripe(k).v;
	unsigned i;

	pr_buf(out, "algo %u sectors %u blocks %u:%u csum %u gran %u",
	       s->algorithm,
	       le16_to_cpu(s->sectors),
	       s->nr_blocks - s->nr_redundant,
	       s->nr_redundant,
	       s->csum_type,
	       1U << s->csum_granularity_bits);

	for (i = 0; i < s->nr_blocks; i++)
		pr_buf(out, " %u:%llu:%u", s->ptrs[i].dev,
		       (u64) s->ptrs[i].offset,
		       stripe_blockcount_get(s, i));
}

static int ptr_matches_stripe(struct bch_fs *c,
			      struct bch_stripe *v,
			      const struct bch_extent_ptr *ptr)
{
	unsigned i;

	for (i = 0; i < v->nr_blocks - v->nr_redundant; i++) {
		const struct bch_extent_ptr *ptr2 = v->ptrs + i;

		if (ptr->dev == ptr2->dev &&
		    ptr->gen == ptr2->gen &&
		    ptr->offset >= ptr2->offset &&
		    ptr->offset <  ptr2->offset + le16_to_cpu(v->sectors))
			return i;
	}

	return -1;
}

static int extent_matches_stripe(struct bch_fs *c,
				 struct bch_stripe *v,
				 struct bkey_s_c k)
{
	struct bkey_s_c_extent e;
	const struct bch_extent_ptr *ptr;
	int idx;

	if (!bkey_extent_is_data(k.k))
		return -1;

	e = bkey_s_c_to_extent(k);

	extent_for_each_ptr(e, ptr) {
		idx = ptr_matches_stripe(c, v, ptr);
		if (idx >= 0)
			return idx;
	}

	return -1;
}

static void ec_stripe_key_init(struct bch_fs *c,
			       struct bkey_i_stripe *s,
			       struct open_buckets *blocks,
			       struct open_buckets *parity,
			       unsigned stripe_size)
{
	struct open_bucket *ob;
	unsigned i, u64s;

	bkey_stripe_init(&s->k_i);
	s->v.sectors			= cpu_to_le16(stripe_size);
	s->v.algorithm			= 0;
	s->v.nr_blocks			= parity->nr + blocks->nr;
	s->v.nr_redundant		= parity->nr;
	s->v.csum_granularity_bits	= ilog2(c->sb.encoded_extent_max);
	s->v.csum_type			= BCH_CSUM_CRC32C;
	s->v.pad			= 0;

	open_bucket_for_each(c, blocks, ob, i)
		s->v.ptrs[i]			= ob->ptr;

	open_bucket_for_each(c, parity, ob, i)
		s->v.ptrs[blocks->nr + i]	= ob->ptr;

	while ((u64s = stripe_val_u64s(&s->v)) > BKEY_VAL_U64s_MAX) {
		BUG_ON(1 << s->v.csum_granularity_bits >=
		       le16_to_cpu(s->v.sectors) ||
		       s->v.csum_granularity_bits == U8_MAX);
		s->v.csum_granularity_bits++;
	}

	set_bkey_val_u64s(&s->k, u64s);
}

/* Checksumming: */

static void ec_generate_checksums(struct ec_stripe_buf *buf)
{
	struct bch_stripe *v = &buf->key.v;
	unsigned csum_granularity = 1 << v->csum_granularity_bits;
	unsigned csums_per_device = stripe_csums_per_device(v);
	unsigned csum_bytes = bch_crc_bytes[v->csum_type];
	unsigned i, j;

	if (!csum_bytes)
		return;

	BUG_ON(buf->offset);
	BUG_ON(buf->size != le16_to_cpu(v->sectors));

	for (i = 0; i < v->nr_blocks; i++) {
		for (j = 0; j < csums_per_device; j++) {
			unsigned offset = j << v->csum_granularity_bits;
			unsigned len = min(csum_granularity, buf->size - offset);

			struct bch_csum csum =
				bch2_checksum(NULL, v->csum_type,
					      null_nonce(),
					      buf->data[i] + (offset << 9),
					      len << 9);

			memcpy(stripe_csum(v, i, j), &csum, csum_bytes);
		}
	}
}

static void ec_validate_checksums(struct bch_fs *c, struct ec_stripe_buf *buf)
{
	struct bch_stripe *v = &buf->key.v;
	unsigned csum_granularity = 1 << v->csum_granularity_bits;
	unsigned csum_bytes = bch_crc_bytes[v->csum_type];
	unsigned i;

	if (!csum_bytes)
		return;

	for (i = 0; i < v->nr_blocks; i++) {
		unsigned offset = buf->offset;
		unsigned end = buf->offset + buf->size;

		if (!test_bit(i, buf->valid))
			continue;

		while (offset < end) {
			unsigned j = offset >> v->csum_granularity_bits;
			unsigned len = min(csum_granularity, end - offset);
			struct bch_csum csum;

			BUG_ON(offset & (csum_granularity - 1));
			BUG_ON(offset + len != le16_to_cpu(v->sectors) &&
			       ((offset + len) & (csum_granularity - 1)));

			csum = bch2_checksum(NULL, v->csum_type,
					     null_nonce(),
					     buf->data[i] + ((offset - buf->offset) << 9),
					     len << 9);

			if (memcmp(stripe_csum(v, i, j), &csum, csum_bytes)) {
				__bcache_io_error(c,
					"checksum error while doing reconstruct read (%u:%u)",
					i, j);
				clear_bit(i, buf->valid);
				break;
			}

			offset += len;
		}
	}
}

/* Erasure coding: */

static void ec_generate_ec(struct ec_stripe_buf *buf)
{
	struct bch_stripe *v = &buf->key.v;
	unsigned nr_data = v->nr_blocks - v->nr_redundant;
	unsigned bytes = le16_to_cpu(v->sectors) << 9;

	raid_gen(nr_data, v->nr_redundant, bytes, buf->data);
}

static unsigned __ec_nr_failed(struct ec_stripe_buf *buf, unsigned nr)
{
	return nr - bitmap_weight(buf->valid, nr);
}

static unsigned ec_nr_failed(struct ec_stripe_buf *buf)
{
	return __ec_nr_failed(buf, buf->key.v.nr_blocks);
}

static int ec_do_recov(struct bch_fs *c, struct ec_stripe_buf *buf)
{
	struct bch_stripe *v = &buf->key.v;
	unsigned i, failed[EC_STRIPE_MAX], nr_failed = 0;
	unsigned nr_data = v->nr_blocks - v->nr_redundant;
	unsigned bytes = buf->size << 9;

	if (ec_nr_failed(buf) > v->nr_redundant) {
		__bcache_io_error(c,
			"error doing reconstruct read: unable to read enough blocks");
		return -1;
	}

	for (i = 0; i < nr_data; i++)
		if (!test_bit(i, buf->valid))
			failed[nr_failed++] = i;

	raid_rec(nr_failed, failed, nr_data, v->nr_redundant, bytes, buf->data);
	return 0;
}

/* IO: */

static void ec_block_endio(struct bio *bio)
{
	struct ec_bio *ec_bio = container_of(bio, struct ec_bio, bio);
	struct bch_dev *ca = ec_bio->ca;
	struct closure *cl = bio->bi_private;

	if (bch2_dev_io_err_on(bio->bi_status, ca, "erasure coding"))
		clear_bit(ec_bio->idx, ec_bio->buf->valid);

	bio_put(&ec_bio->bio);
	percpu_ref_put(&ca->io_ref);
	closure_put(cl);
}

static void ec_block_io(struct bch_fs *c, struct ec_stripe_buf *buf,
			unsigned rw, unsigned idx, struct closure *cl)
{
	struct bch_stripe *v = &buf->key.v;
	unsigned offset = 0, bytes = buf->size << 9;
	struct bch_extent_ptr *ptr = &v->ptrs[idx];
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);

	if (!bch2_dev_get_ioref(ca, rw)) {
		clear_bit(idx, buf->valid);
		return;
	}

	while (offset < bytes) {
		unsigned nr_iovecs = min_t(size_t, BIO_MAX_VECS,
					   DIV_ROUND_UP(bytes, PAGE_SIZE));
		unsigned b = min_t(size_t, bytes - offset,
				   nr_iovecs << PAGE_SHIFT);
		struct ec_bio *ec_bio;

		ec_bio = container_of(bio_alloc_bioset(ca->disk_sb.bdev,
						       nr_iovecs,
						       rw,
						       GFP_KERNEL,
						       &c->ec_bioset),
				      struct ec_bio, bio);

		ec_bio->ca			= ca;
		ec_bio->buf			= buf;
		ec_bio->idx			= idx;

		ec_bio->bio.bi_iter.bi_sector	= ptr->offset + buf->offset + (offset >> 9);
		ec_bio->bio.bi_iter.bi_size	= b;
		ec_bio->bio.bi_end_io		= ec_block_endio;
		ec_bio->bio.bi_private		= cl;

		bch2_bio_map(&ec_bio->bio, buf->data[idx] + offset);

		closure_get(cl);
		percpu_ref_get(&ca->io_ref);

		submit_bio(&ec_bio->bio);

		offset += b;
	}

	percpu_ref_put(&ca->io_ref);
}

/* recovery read path: */
int bch2_ec_read_extent(struct bch_fs *c, struct bch_read_bio *rbio)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct ec_stripe_buf *buf;
	struct closure cl;
	struct bkey_s_c k;
	struct bch_stripe *v;
	unsigned stripe_idx;
	unsigned offset, end;
	unsigned i, nr_data, csum_granularity;
	int ret = 0, idx;

	closure_init_stack(&cl);

	BUG_ON(!rbio->pick.idx ||
	       rbio->pick.idx - 1 >= rbio->pick.ec_nr);

	stripe_idx = rbio->pick.ec[rbio->pick.idx - 1].idx;

	buf = kzalloc(sizeof(*buf), GFP_NOIO);
	if (!buf)
		return -ENOMEM;

	bch2_trans_init(&trans, c);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EC,
				   POS(0, stripe_idx),
				   BTREE_ITER_SLOTS);
	k = bch2_btree_iter_peek_slot(iter);
	if (btree_iter_err(k) || k.k->type != KEY_TYPE_stripe) {
		__bcache_io_error(c,
			"error doing reconstruct read: stripe not found");
		kfree(buf);
		return bch2_trans_exit(&trans) ?: -EIO;
	}

	bkey_reassemble(&buf->key.k_i, k);
	bch2_trans_exit(&trans);

	v = &buf->key.v;

	nr_data = v->nr_blocks - v->nr_redundant;

	idx = ptr_matches_stripe(c, v, &rbio->pick.ptr);
	BUG_ON(idx < 0);

	csum_granularity = 1U << v->csum_granularity_bits;

	offset	= rbio->bio.bi_iter.bi_sector - v->ptrs[idx].offset;
	end	= offset + bio_sectors(&rbio->bio);

	BUG_ON(end > le16_to_cpu(v->sectors));

	buf->offset	= round_down(offset, csum_granularity);
	buf->size	= min_t(unsigned, le16_to_cpu(v->sectors),
				round_up(end, csum_granularity)) - buf->offset;

	for (i = 0; i < v->nr_blocks; i++) {
		buf->data[i] = kmalloc(buf->size << 9, GFP_NOIO);
		if (!buf->data[i]) {
			ret = -ENOMEM;
			goto err;
		}
	}

	memset(buf->valid, 0xFF, sizeof(buf->valid));

	for (i = 0; i < v->nr_blocks; i++) {
		struct bch_extent_ptr *ptr = v->ptrs + i;
		struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);

		if (ptr_stale(ca, ptr)) {
			__bcache_io_error(c,
					  "error doing reconstruct read: stale pointer");
			clear_bit(i, buf->valid);
			continue;
		}

		ec_block_io(c, buf, REQ_OP_READ, i, &cl);
	}

	closure_sync(&cl);

	if (ec_nr_failed(buf) > v->nr_redundant) {
		__bcache_io_error(c,
			"error doing reconstruct read: unable to read enough blocks");
		ret = -EIO;
		goto err;
	}

	ec_validate_checksums(c, buf);

	ret = ec_do_recov(c, buf);
	if (ret)
		goto err;

	memcpy_to_bio(&rbio->bio, rbio->bio.bi_iter,
		      buf->data[idx] + ((offset - buf->offset) << 9));
err:
	for (i = 0; i < v->nr_blocks; i++)
		kfree(buf->data[i]);
	kfree(buf);
	return ret;
}

/* stripe bucket accounting: */

static int __ec_stripe_mem_alloc(struct bch_fs *c, size_t idx, gfp_t gfp)
{
	ec_stripes_heap n, *h = &c->ec_stripes_heap;

	if (idx >= h->size) {
		if (!init_heap(&n, max(1024UL, roundup_pow_of_two(idx + 1)), gfp))
			return -ENOMEM;

		spin_lock(&c->ec_stripes_heap_lock);
		if (n.size > h->size) {
			memcpy(n.data, h->data, h->used * sizeof(h->data[0]));
			n.used = h->used;
			swap(*h, n);
		}
		spin_unlock(&c->ec_stripes_heap_lock);

		free_heap(&n);
	}

	if (!genradix_ptr_alloc(&c->stripes[0], idx, gfp))
		return -ENOMEM;

	if (c->gc_pos.phase != GC_PHASE_NOT_RUNNING &&
	    !genradix_ptr_alloc(&c->stripes[1], idx, gfp))
		return -ENOMEM;

	return 0;
}

static int ec_stripe_mem_alloc(struct bch_fs *c,
			       struct btree_iter *iter)
{
	size_t idx = iter->pos.offset;

	if (!__ec_stripe_mem_alloc(c, idx, GFP_NOWAIT|__GFP_NOWARN))
		return 0;

	bch2_btree_iter_unlock(iter);

	if (!__ec_stripe_mem_alloc(c, idx, GFP_KERNEL))
		return -EINTR;
	return -ENOMEM;
}

static ssize_t stripe_idx_to_delete(struct bch_fs *c)
{
	ec_stripes_heap *h = &c->ec_stripes_heap;

	return h->data[0].blocks_nonempty == 0 ? h->data[0].idx : -1;
}

static inline int ec_stripes_heap_cmp(ec_stripes_heap *h,
				      struct ec_stripe_heap_entry l,
				      struct ec_stripe_heap_entry r)
{
	return ((l.blocks_nonempty > r.blocks_nonempty) -
		(l.blocks_nonempty < r.blocks_nonempty));
}

static inline void ec_stripes_heap_set_backpointer(ec_stripes_heap *h,
						   size_t i)
{
	struct bch_fs *c = container_of(h, struct bch_fs, ec_stripes_heap);

	genradix_ptr(&c->stripes[0], h->data[i].idx)->heap_idx = i;
}

static void heap_verify_backpointer(struct bch_fs *c, size_t idx)
{
	ec_stripes_heap *h = &c->ec_stripes_heap;
	struct stripe *m = genradix_ptr(&c->stripes[0], idx);

	BUG_ON(!m->alive);
	BUG_ON(m->heap_idx >= h->used);
	BUG_ON(h->data[m->heap_idx].idx != idx);
}

void bch2_stripes_heap_update(struct bch_fs *c,
			      struct stripe *m, size_t idx)
{
	ec_stripes_heap *h = &c->ec_stripes_heap;
	size_t i;

	heap_verify_backpointer(c, idx);

	h->data[m->heap_idx].blocks_nonempty = m->blocks_nonempty;

	i = m->heap_idx;
	heap_sift_up(h,	  i, ec_stripes_heap_cmp,
		     ec_stripes_heap_set_backpointer);
	heap_sift_down(h, i, ec_stripes_heap_cmp,
		       ec_stripes_heap_set_backpointer);

	heap_verify_backpointer(c, idx);

	if (stripe_idx_to_delete(c) >= 0)
		schedule_work(&c->ec_stripe_delete_work);
}

void bch2_stripes_heap_del(struct bch_fs *c,
			   struct stripe *m, size_t idx)
{
	heap_verify_backpointer(c, idx);

	m->alive = false;
	heap_del(&c->ec_stripes_heap, m->heap_idx,
		 ec_stripes_heap_cmp,
		 ec_stripes_heap_set_backpointer);
}

void bch2_stripes_heap_insert(struct bch_fs *c,
			      struct stripe *m, size_t idx)
{
	BUG_ON(heap_full(&c->ec_stripes_heap));

	heap_add(&c->ec_stripes_heap, ((struct ec_stripe_heap_entry) {
			.idx = idx,
			.blocks_nonempty = m->blocks_nonempty,
		}),
		 ec_stripes_heap_cmp,
		 ec_stripes_heap_set_backpointer);
	m->alive = true;

	heap_verify_backpointer(c, idx);
}

/* stripe deletion */

static int ec_stripe_delete(struct bch_fs *c, size_t idx)
{
	return bch2_btree_delete_range(c, BTREE_ID_EC,
				       POS(0, idx),
				       POS(0, idx + 1),
				       NULL);
}

static void ec_stripe_delete_work(struct work_struct *work)
{
	struct bch_fs *c =
		container_of(work, struct bch_fs, ec_stripe_delete_work);
	ssize_t idx;

	down_read(&c->gc_lock);
	mutex_lock(&c->ec_stripe_create_lock);

	while (1) {
		spin_lock(&c->ec_stripes_heap_lock);
		idx = stripe_idx_to_delete(c);
		spin_unlock(&c->ec_stripes_heap_lock);

		if (idx < 0)
			break;

		ec_stripe_delete(c, idx);
	}

	mutex_unlock(&c->ec_stripe_create_lock);
	up_read(&c->gc_lock);
}

/* stripe creation: */

static int ec_stripe_bkey_insert(struct bch_fs *c,
				 struct bkey_i_stripe *stripe)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c);
retry:
	bch2_trans_begin(&trans);

	/* XXX: start pos hint */
	iter = bch2_trans_get_iter(&trans, BTREE_ID_EC, POS_MIN,
				   BTREE_ITER_SLOTS|BTREE_ITER_INTENT);

	for_each_btree_key_continue(iter, BTREE_ITER_SLOTS|BTREE_ITER_INTENT, k) {
		if (bkey_cmp(k.k->p, POS(0, U32_MAX)) > 0)
			break;

		if (bkey_deleted(k.k))
			goto found_slot;
	}

	ret = -ENOSPC;
	goto out;
found_slot:
	ret = ec_stripe_mem_alloc(c, iter);

	if (ret == -EINTR)
		goto retry;
	if (ret)
		return ret;

	stripe->k.p = iter->pos;

	bch2_trans_update(&trans, BTREE_INSERT_ENTRY(iter, &stripe->k_i));

	ret = bch2_trans_commit(&trans, NULL, NULL,
				BTREE_INSERT_NOFAIL|
				BTREE_INSERT_USE_RESERVE);
out:
	bch2_trans_exit(&trans);

	return ret;
}

static void extent_stripe_ptr_add(struct bkey_s_extent e,
				  struct ec_stripe_buf *s,
				  struct bch_extent_ptr *ptr,
				  unsigned block)
{
	struct bch_extent_stripe_ptr *dst = (void *) ptr;
	union bch_extent_entry *end = extent_entry_last(e);

	memmove_u64s_up(dst + 1, dst, (u64 *) end - (u64 *) dst);
	e.k->u64s += sizeof(*dst) / sizeof(u64);

	*dst = (struct bch_extent_stripe_ptr) {
		.type = 1 << BCH_EXTENT_ENTRY_stripe_ptr,
		.block		= block,
		.idx		= s->key.k.p.offset,
	};
}

static int ec_stripe_update_ptrs(struct bch_fs *c,
				 struct ec_stripe_buf *s,
				 struct bkey *pos)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_s_extent e;
	struct bch_extent_ptr *ptr;
	BKEY_PADDED(k) tmp;
	int ret = 0, dev, idx;

	bch2_trans_init(&trans, c);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS,
				   bkey_start_pos(pos),
				   BTREE_ITER_INTENT);

	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = btree_iter_err(k)) &&
	       bkey_cmp(bkey_start_pos(k.k), pos->p) < 0) {
		idx = extent_matches_stripe(c, &s->key.v, k);
		if (idx < 0) {
			bch2_btree_iter_next(iter);
			continue;
		}

		dev = s->key.v.ptrs[idx].dev;

		bkey_reassemble(&tmp.k, k);
		e = bkey_i_to_s_extent(&tmp.k);

		extent_for_each_ptr(e, ptr)
			if (ptr->dev != dev)
				ptr->cached = true;

		ptr = (void *) bch2_extent_has_device(e.c, dev);
		BUG_ON(!ptr);

		extent_stripe_ptr_add(e, s, ptr, idx);

		bch2_trans_update(&trans, BTREE_INSERT_ENTRY(iter, &tmp.k));

		ret = bch2_trans_commit(&trans, NULL, NULL,
					BTREE_INSERT_ATOMIC|
					BTREE_INSERT_NOFAIL|
					BTREE_INSERT_USE_RESERVE);
		if (ret == -EINTR)
			ret = 0;
		if (ret)
			break;
	}

	bch2_trans_exit(&trans);

	return ret;
}

/*
 * data buckets of new stripe all written: create the stripe
 */
static void ec_stripe_create(struct ec_stripe_new *s)
{
	struct bch_fs *c = s->c;
	struct open_bucket *ob;
	struct bkey_i *k;
	struct bch_stripe *v = &s->stripe.key.v;
	unsigned i, nr_data = v->nr_blocks - v->nr_redundant;
	struct closure cl;
	int ret;

	BUG_ON(s->h->s == s);

	closure_init_stack(&cl);

	if (s->err) {
		bch_err(c, "error creating stripe: error writing data buckets");
		goto err;
	}

	if (!percpu_ref_tryget(&c->writes))
		goto err;

	BUG_ON(bitmap_weight(s->blocks_allocated,
			     s->blocks.nr) != s->blocks.nr);

	ec_generate_ec(&s->stripe);

	ec_generate_checksums(&s->stripe);

	/* write p/q: */
	for (i = nr_data; i < v->nr_blocks; i++)
		ec_block_io(c, &s->stripe, REQ_OP_WRITE, i, &cl);

	closure_sync(&cl);

	for (i = nr_data; i < v->nr_blocks; i++)
		if (!test_bit(i, s->stripe.valid)) {
			bch_err(c, "error creating stripe: error writing redundancy buckets");
			goto err_put_writes;
		}

	mutex_lock(&c->ec_stripe_create_lock);

	ret = ec_stripe_bkey_insert(c, &s->stripe.key);
	if (ret) {
		bch_err(c, "error creating stripe: error creating stripe key");
		goto err_unlock;
	}

	for_each_keylist_key(&s->keys, k) {
		ret = ec_stripe_update_ptrs(c, &s->stripe, &k->k);
		if (ret)
			break;
	}

err_unlock:
	mutex_unlock(&c->ec_stripe_create_lock);
err_put_writes:
	percpu_ref_put(&c->writes);
err:
	open_bucket_for_each(c, &s->blocks, ob, i) {
		ob->ec = NULL;
		__bch2_open_bucket_put(c, ob);
	}

	bch2_open_buckets_put(c, &s->parity);

	bch2_keylist_free(&s->keys, s->inline_keys);

	mutex_lock(&s->h->lock);
	list_del(&s->list);
	mutex_unlock(&s->h->lock);

	for (i = 0; i < s->stripe.key.v.nr_blocks; i++)
		kvpfree(s->stripe.data[i], s->stripe.size << 9);
	kfree(s);
}

static struct ec_stripe_new *ec_stripe_set_pending(struct ec_stripe_head *h)
{
	struct ec_stripe_new *s = h->s;

	list_add(&s->list, &h->stripes);
	h->s = NULL;

	return s;
}

static void ec_stripe_new_put(struct ec_stripe_new *s)
{
	BUG_ON(atomic_read(&s->pin) <= 0);
	if (atomic_dec_and_test(&s->pin))
		ec_stripe_create(s);
}

/* have a full bucket - hand it off to be erasure coded: */
void bch2_ec_bucket_written(struct bch_fs *c, struct open_bucket *ob)
{
	struct ec_stripe_new *s = ob->ec;

	if (ob->sectors_free)
		s->err = -1;

	ec_stripe_new_put(s);
}

void bch2_ec_bucket_cancel(struct bch_fs *c, struct open_bucket *ob)
{
	struct ec_stripe_new *s = ob->ec;

	s->err = -EIO;
}

void *bch2_writepoint_ec_buf(struct bch_fs *c, struct write_point *wp)
{
	struct open_bucket *ob = ec_open_bucket(c, &wp->ptrs);
	struct bch_dev *ca;
	unsigned offset;

	if (!ob)
		return NULL;

	ca	= bch_dev_bkey_exists(c, ob->ptr.dev);
	offset	= ca->mi.bucket_size - ob->sectors_free;

	return ob->ec->stripe.data[ob->ec_idx] + (offset << 9);
}

void bch2_ec_add_backpointer(struct bch_fs *c, struct write_point *wp,
			     struct bpos pos, unsigned sectors)
{
	struct open_bucket *ob = ec_open_bucket(c, &wp->ptrs);
	struct ec_stripe_new *ec;

	if (!ob)
		return;

	ec = ob->ec;
	mutex_lock(&ec->lock);

	if (bch2_keylist_realloc(&ec->keys, ec->inline_keys,
				 ARRAY_SIZE(ec->inline_keys),
				 BKEY_U64s)) {
		BUG();
	}

	bkey_init(&ec->keys.top->k);
	ec->keys.top->k.p	= pos;
	bch2_key_resize(&ec->keys.top->k, sectors);
	bch2_keylist_push(&ec->keys);

	mutex_unlock(&ec->lock);
}

static int unsigned_cmp(const void *_l, const void *_r)
{
	unsigned l = *((const unsigned *) _l);
	unsigned r = *((const unsigned *) _r);

	return (l > r) - (l < r);
}

/* pick most common bucket size: */
static unsigned pick_blocksize(struct bch_fs *c,
			       struct bch_devs_mask *devs)
{
	struct bch_dev *ca;
	unsigned i, nr = 0, sizes[BCH_SB_MEMBERS_MAX];
	struct {
		unsigned nr, size;
	} cur = { 0, 0 }, best = { 0, 0 };

	for_each_member_device_rcu(ca, c, i, devs)
		sizes[nr++] = ca->mi.bucket_size;

	sort(sizes, nr, sizeof(unsigned), unsigned_cmp, NULL);

	for (i = 0; i < nr; i++) {
		if (sizes[i] != cur.size) {
			if (cur.nr > best.nr)
				best = cur;

			cur.nr = 0;
			cur.size = sizes[i];
		}

		cur.nr++;
	}

	if (cur.nr > best.nr)
		best = cur;

	return best.size;
}

int bch2_ec_stripe_new_alloc(struct bch_fs *c, struct ec_stripe_head *h)
{
	struct ec_stripe_new *s;
	unsigned i;

	BUG_ON(h->parity.nr != h->redundancy);
	BUG_ON(!h->blocks.nr);
	BUG_ON(h->parity.nr + h->blocks.nr > EC_STRIPE_MAX);
	lockdep_assert_held(&h->lock);

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	mutex_init(&s->lock);
	atomic_set(&s->pin, 1);
	s->c		= c;
	s->h		= h;
	s->blocks	= h->blocks;
	s->parity	= h->parity;

	memset(&h->blocks, 0, sizeof(h->blocks));
	memset(&h->parity, 0, sizeof(h->parity));

	bch2_keylist_init(&s->keys, s->inline_keys);

	s->stripe.offset	= 0;
	s->stripe.size		= h->blocksize;
	memset(s->stripe.valid, 0xFF, sizeof(s->stripe.valid));

	ec_stripe_key_init(c, &s->stripe.key,
			   &s->blocks, &s->parity,
			   h->blocksize);

	for (i = 0; i < s->stripe.key.v.nr_blocks; i++) {
		s->stripe.data[i] = kvpmalloc(s->stripe.size << 9, GFP_KERNEL);
		if (!s->stripe.data[i])
			goto err;
	}

	h->s = s;

	return 0;
err:
	for (i = 0; i < s->stripe.key.v.nr_blocks; i++)
		kvpfree(s->stripe.data[i], s->stripe.size << 9);
	kfree(s);
	return -ENOMEM;
}

static struct ec_stripe_head *
ec_new_stripe_head_alloc(struct bch_fs *c, unsigned target,
			 unsigned algo, unsigned redundancy)
{
	struct ec_stripe_head *h;
	struct bch_dev *ca;
	unsigned i;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return NULL;

	mutex_init(&h->lock);
	mutex_lock(&h->lock);
	INIT_LIST_HEAD(&h->stripes);

	h->target	= target;
	h->algo		= algo;
	h->redundancy	= redundancy;

	rcu_read_lock();
	h->devs = target_rw_devs(c, BCH_DATA_USER, target);

	for_each_member_device_rcu(ca, c, i, &h->devs)
		if (!ca->mi.durability)
			__clear_bit(i, h->devs.d);

	h->blocksize = pick_blocksize(c, &h->devs);

	for_each_member_device_rcu(ca, c, i, &h->devs)
		if (ca->mi.bucket_size == h->blocksize)
			h->nr_active_devs++;

	rcu_read_unlock();
	list_add(&h->list, &c->ec_new_stripe_list);
	return h;
}

void bch2_ec_stripe_head_put(struct ec_stripe_head *h)
{
	struct ec_stripe_new *s = NULL;

	if (h->s &&
	    bitmap_weight(h->s->blocks_allocated,
			  h->s->blocks.nr) == h->s->blocks.nr)
		s = ec_stripe_set_pending(h);

	mutex_unlock(&h->lock);

	if (s)
		ec_stripe_new_put(s);
}

struct ec_stripe_head *bch2_ec_stripe_head_get(struct bch_fs *c,
					       unsigned target,
					       unsigned algo,
					       unsigned redundancy)
{
	struct ec_stripe_head *h;

	if (!redundancy)
		return NULL;

	mutex_lock(&c->ec_new_stripe_lock);
	list_for_each_entry(h, &c->ec_new_stripe_list, list)
		if (h->target		== target &&
		    h->algo		== algo &&
		    h->redundancy	== redundancy) {
			mutex_lock(&h->lock);
			goto found;
		}

	h = ec_new_stripe_head_alloc(c, target, algo, redundancy);
found:
	mutex_unlock(&c->ec_new_stripe_lock);
	return h;
}

void bch2_ec_stop_dev(struct bch_fs *c, struct bch_dev *ca)
{
	struct ec_stripe_head *h;
	struct open_bucket *ob;
	unsigned i;

	mutex_lock(&c->ec_new_stripe_lock);
	list_for_each_entry(h, &c->ec_new_stripe_list, list) {
		struct ec_stripe_new *s = NULL;

		mutex_lock(&h->lock);
		bch2_open_buckets_stop_dev(c, ca,
					   &h->blocks,
					   BCH_DATA_USER);
		bch2_open_buckets_stop_dev(c, ca,
					   &h->parity,
					   BCH_DATA_USER);

		if (!h->s)
			goto unlock;

		open_bucket_for_each(c, &h->s->blocks, ob, i)
			if (ob->ptr.dev == ca->dev_idx)
				goto found;
		open_bucket_for_each(c, &h->s->parity, ob, i)
			if (ob->ptr.dev == ca->dev_idx)
				goto found;
		goto unlock;
found:
		h->s->err = -1;
		s = ec_stripe_set_pending(h);
unlock:
		mutex_unlock(&h->lock);

		if (s)
			ec_stripe_new_put(s);
	}
	mutex_unlock(&c->ec_new_stripe_lock);
}

static int __bch2_stripe_write_key(struct btree_trans *trans,
				   struct btree_iter *iter,
				   struct stripe *m,
				   size_t idx,
				   struct bkey_i_stripe *new_key,
				   unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k;
	unsigned i;
	int ret;

	bch2_btree_iter_set_pos(iter, POS(0, idx));

	k = bch2_btree_iter_peek_slot(iter);
	ret = btree_iter_err(k);
	if (ret)
		return ret;

	if (k.k->type != KEY_TYPE_stripe)
		return -EIO;

	bkey_reassemble(&new_key->k_i, k);

	spin_lock(&c->ec_stripes_heap_lock);

	for (i = 0; i < new_key->v.nr_blocks; i++)
		stripe_blockcount_set(&new_key->v, i,
				      m->block_sectors[i]);
	m->dirty = false;

	spin_unlock(&c->ec_stripes_heap_lock);

	bch2_trans_update(trans, BTREE_INSERT_ENTRY(iter, &new_key->k_i));

	return bch2_trans_commit(trans, NULL, NULL,
				 BTREE_INSERT_NOFAIL|flags);
}

int bch2_stripes_write(struct bch_fs *c, bool *wrote)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct genradix_iter giter;
	struct bkey_i_stripe *new_key;
	struct stripe *m;
	int ret = 0;

	new_key = kmalloc(255 * sizeof(u64), GFP_KERNEL);
	BUG_ON(!new_key);

	bch2_trans_init(&trans, c);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EC, POS_MIN,
				   BTREE_ITER_SLOTS|BTREE_ITER_INTENT);

	genradix_for_each(&c->stripes[0], giter, m) {
		if (!m->dirty)
			continue;

		ret = __bch2_stripe_write_key(&trans, iter, m, giter.pos,
					new_key, BTREE_INSERT_NOCHECK_RW);
		if (ret)
			break;

		*wrote = true;
	}

	bch2_trans_exit(&trans);

	kfree(new_key);

	return ret;
}

static void bch2_stripe_read_key(struct bch_fs *c, struct bkey_s_c k)
{

	struct gc_pos pos = { 0 };

	bch2_mark_key(c, k, true, 0, pos, NULL, 0, 0);
}

int bch2_stripes_read(struct bch_fs *c, struct list_head *journal_replay_list)
{
	struct journal_replay *r;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	ret = bch2_fs_ec_start(c);
	if (ret)
		return ret;

	bch2_trans_init(&trans, c);

	for_each_btree_key(&trans, iter, BTREE_ID_EC, POS_MIN, 0, k) {
		bch2_stripe_read_key(c, k);
		bch2_trans_cond_resched(&trans);
	}

	ret = bch2_trans_exit(&trans);
	if (ret)
		return ret;

	list_for_each_entry(r, journal_replay_list, list) {
		struct bkey_i *k, *n;
		struct jset_entry *entry;

		for_each_jset_key(k, n, entry, &r->j)
			if (entry->btree_id == BTREE_ID_EC)
				bch2_stripe_read_key(c, bkey_i_to_s_c(k));
	}

	return 0;
}

int bch2_ec_mem_alloc(struct bch_fs *c, bool gc)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	size_t i, idx = 0;
	int ret = 0;

	bch2_trans_init(&trans, c);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EC, POS(0, U64_MAX), 0);

	k = bch2_btree_iter_prev(iter);
	if (!IS_ERR_OR_NULL(k.k))
		idx = k.k->p.offset + 1;
	ret = bch2_trans_exit(&trans);
	if (ret)
		return ret;

	if (!gc &&
	    !init_heap(&c->ec_stripes_heap, roundup_pow_of_two(idx),
		       GFP_KERNEL))
		return -ENOMEM;
#if 0
	ret = genradix_prealloc(&c->stripes[gc], idx, GFP_KERNEL);
#else
	for (i = 0; i < idx; i++)
		if (!genradix_ptr_alloc(&c->stripes[gc], i, GFP_KERNEL))
			return -ENOMEM;
#endif
	return 0;
}

int bch2_fs_ec_start(struct bch_fs *c)
{
	return bch2_ec_mem_alloc(c, false);
}

void bch2_fs_ec_exit(struct bch_fs *c)
{
	struct ec_stripe_head *h;

	while (1) {
		mutex_lock(&c->ec_new_stripe_lock);
		h = list_first_entry_or_null(&c->ec_new_stripe_list,
					     struct ec_stripe_head, list);
		if (h)
			list_del(&h->list);
		mutex_unlock(&c->ec_new_stripe_lock);
		if (!h)
			break;

		BUG_ON(h->s);
		BUG_ON(!list_empty(&h->stripes));
		kfree(h);
	}

	free_heap(&c->ec_stripes_heap);
	genradix_free(&c->stripes[0]);
	bioset_exit(&c->ec_bioset);
}

int bch2_fs_ec_init(struct bch_fs *c)
{
	INIT_WORK(&c->ec_stripe_delete_work, ec_stripe_delete_work);

	return bioset_init(&c->ec_bioset, 1, offsetof(struct ec_bio, bio),
			   BIOSET_NEED_BVECS);
}
