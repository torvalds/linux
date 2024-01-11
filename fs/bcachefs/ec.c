// SPDX-License-Identifier: GPL-2.0

/* erasure coding */

#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "backpointers.h"
#include "bkey_buf.h"
#include "bset.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "btree_write_buffer.h"
#include "buckets.h"
#include "checksum.h"
#include "disk_groups.h"
#include "ec.h"
#include "error.h"
#include "io_read.h"
#include "keylist.h"
#include "recovery.h"
#include "replicas.h"
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

int bch2_stripe_invalid(struct bch_fs *c, struct bkey_s_c k,
			enum bkey_invalid_flags flags,
			struct printbuf *err)
{
	const struct bch_stripe *s = bkey_s_c_to_stripe(k).v;
	int ret = 0;

	bkey_fsck_err_on(bkey_eq(k.k->p, POS_MIN) ||
			 bpos_gt(k.k->p, POS(0, U32_MAX)), c, err,
			 stripe_pos_bad,
			 "stripe at bad pos");

	bkey_fsck_err_on(bkey_val_u64s(k.k) < stripe_val_u64s(s), c, err,
			 stripe_val_size_bad,
			 "incorrect value size (%zu < %u)",
			 bkey_val_u64s(k.k), stripe_val_u64s(s));

	ret = bch2_bkey_ptrs_invalid(c, k, flags, err);
fsck_err:
	return ret;
}

void bch2_stripe_to_text(struct printbuf *out, struct bch_fs *c,
			 struct bkey_s_c k)
{
	const struct bch_stripe *s = bkey_s_c_to_stripe(k).v;
	unsigned i, nr_data = s->nr_blocks - s->nr_redundant;

	prt_printf(out, "algo %u sectors %u blocks %u:%u csum %u gran %u",
	       s->algorithm,
	       le16_to_cpu(s->sectors),
	       nr_data,
	       s->nr_redundant,
	       s->csum_type,
	       1U << s->csum_granularity_bits);

	for (i = 0; i < s->nr_blocks; i++) {
		const struct bch_extent_ptr *ptr = s->ptrs + i;
		struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
		u32 offset;
		u64 b = sector_to_bucket_and_offset(ca, ptr->offset, &offset);

		prt_printf(out, " %u:%llu:%u", ptr->dev, b, offset);
		if (i < nr_data)
			prt_printf(out, "#%u", stripe_blockcount_get(s, i));
		prt_printf(out, " gen %u", ptr->gen);
		if (ptr_stale(ca, ptr))
			prt_printf(out, " stale");
	}
}

/* Triggers: */

static int bch2_trans_mark_stripe_bucket(struct btree_trans *trans,
					 struct bkey_s_c_stripe s,
					 unsigned idx, bool deleting)
{
	struct bch_fs *c = trans->c;
	const struct bch_extent_ptr *ptr = &s.v->ptrs[idx];
	struct btree_iter iter;
	struct bkey_i_alloc_v4 *a;
	enum bch_data_type data_type = idx >= s.v->nr_blocks - s.v->nr_redundant
		? BCH_DATA_parity : 0;
	s64 sectors = data_type ? le16_to_cpu(s.v->sectors) : 0;
	int ret = 0;

	if (deleting)
		sectors = -sectors;

	a = bch2_trans_start_alloc_update(trans, &iter, PTR_BUCKET_POS(c, ptr));
	if (IS_ERR(a))
		return PTR_ERR(a);

	ret = bch2_check_bucket_ref(trans, s.s_c, ptr, sectors, data_type,
				    a->v.gen, a->v.data_type,
				    a->v.dirty_sectors);
	if (ret)
		goto err;

	if (!deleting) {
		if (bch2_trans_inconsistent_on(a->v.stripe ||
					       a->v.stripe_redundancy, trans,
				"bucket %llu:%llu gen %u data type %s dirty_sectors %u: multiple stripes using same bucket (%u, %llu)",
				iter.pos.inode, iter.pos.offset, a->v.gen,
				bch2_data_types[a->v.data_type],
				a->v.dirty_sectors,
				a->v.stripe, s.k->p.offset)) {
			ret = -EIO;
			goto err;
		}

		if (bch2_trans_inconsistent_on(data_type && a->v.dirty_sectors, trans,
				"bucket %llu:%llu gen %u data type %s dirty_sectors %u: data already in stripe bucket %llu",
				iter.pos.inode, iter.pos.offset, a->v.gen,
				bch2_data_types[a->v.data_type],
				a->v.dirty_sectors,
				s.k->p.offset)) {
			ret = -EIO;
			goto err;
		}

		a->v.stripe		= s.k->p.offset;
		a->v.stripe_redundancy	= s.v->nr_redundant;
		a->v.data_type		= BCH_DATA_stripe;
	} else {
		if (bch2_trans_inconsistent_on(a->v.stripe != s.k->p.offset ||
					       a->v.stripe_redundancy != s.v->nr_redundant, trans,
				"bucket %llu:%llu gen %u: not marked as stripe when deleting stripe %llu (got %u)",
				iter.pos.inode, iter.pos.offset, a->v.gen,
				s.k->p.offset, a->v.stripe)) {
			ret = -EIO;
			goto err;
		}

		a->v.stripe		= 0;
		a->v.stripe_redundancy	= 0;
		a->v.data_type		= alloc_data_type(a->v, BCH_DATA_user);
	}

	a->v.dirty_sectors += sectors;
	if (data_type)
		a->v.data_type = !deleting ? data_type : 0;

	ret = bch2_trans_update(trans, &iter, &a->k_i, 0);
	if (ret)
		goto err;
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int mark_stripe_bucket(struct btree_trans *trans,
			      struct bkey_s_c k,
			      unsigned ptr_idx,
			      unsigned flags)
{
	struct bch_fs *c = trans->c;
	const struct bch_stripe *s = bkey_s_c_to_stripe(k).v;
	unsigned nr_data = s->nr_blocks - s->nr_redundant;
	bool parity = ptr_idx >= nr_data;
	enum bch_data_type data_type = parity ? BCH_DATA_parity : BCH_DATA_stripe;
	s64 sectors = parity ? le16_to_cpu(s->sectors) : 0;
	const struct bch_extent_ptr *ptr = s->ptrs + ptr_idx;
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
	struct bucket old, new, *g;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	BUG_ON(!(flags & BTREE_TRIGGER_GC));

	/* * XXX doesn't handle deletion */

	percpu_down_read(&c->mark_lock);
	g = PTR_GC_BUCKET(ca, ptr);

	if (g->dirty_sectors ||
	    (g->stripe && g->stripe != k.k->p.offset)) {
		bch2_fs_inconsistent(c,
			      "bucket %u:%zu gen %u: multiple stripes using same bucket\n%s",
			      ptr->dev, PTR_BUCKET_NR(ca, ptr), g->gen,
			      (bch2_bkey_val_to_text(&buf, c, k), buf.buf));
		ret = -EINVAL;
		goto err;
	}

	bucket_lock(g);
	old = *g;

	ret = bch2_check_bucket_ref(trans, k, ptr, sectors, data_type,
				    g->gen, g->data_type,
				    g->dirty_sectors);
	if (ret)
		goto err;

	g->data_type = data_type;
	g->dirty_sectors += sectors;

	g->stripe		= k.k->p.offset;
	g->stripe_redundancy	= s->nr_redundant;
	new = *g;
err:
	bucket_unlock(g);
	if (!ret)
		bch2_dev_usage_update_m(c, ca, &old, &new);
	percpu_up_read(&c->mark_lock);
	printbuf_exit(&buf);
	return ret;
}

int bch2_trigger_stripe(struct btree_trans *trans,
			enum btree_id btree_id, unsigned level,
			struct bkey_s_c old, struct bkey_s _new,
			unsigned flags)
{
	struct bkey_s_c new = _new.s_c;
	struct bch_fs *c = trans->c;
	u64 idx = new.k->p.offset;
	const struct bch_stripe *old_s = old.k->type == KEY_TYPE_stripe
		? bkey_s_c_to_stripe(old).v : NULL;
	const struct bch_stripe *new_s = new.k->type == KEY_TYPE_stripe
		? bkey_s_c_to_stripe(new).v : NULL;

	if (flags & BTREE_TRIGGER_TRANSACTIONAL) {
		/*
		 * If the pointers aren't changing, we don't need to do anything:
		 */
		if (new_s && old_s &&
		    new_s->nr_blocks	== old_s->nr_blocks &&
		    new_s->nr_redundant	== old_s->nr_redundant &&
		    !memcmp(old_s->ptrs, new_s->ptrs,
			    new_s->nr_blocks * sizeof(struct bch_extent_ptr)))
			return 0;

		BUG_ON(new_s && old_s &&
		       (new_s->nr_blocks	!= old_s->nr_blocks ||
			new_s->nr_redundant	!= old_s->nr_redundant));

		if (new_s) {
			s64 sectors = le16_to_cpu(new_s->sectors);

			struct bch_replicas_padded r;
			bch2_bkey_to_replicas(&r.e, new);
			int ret = bch2_update_replicas_list(trans, &r.e, sectors * new_s->nr_redundant);
			if (ret)
				return ret;
		}

		if (old_s) {
			s64 sectors = -((s64) le16_to_cpu(old_s->sectors));

			struct bch_replicas_padded r;
			bch2_bkey_to_replicas(&r.e, old);
			int ret = bch2_update_replicas_list(trans, &r.e, sectors * old_s->nr_redundant);
			if (ret)
				return ret;
		}

		unsigned nr_blocks = new_s ? new_s->nr_blocks : old_s->nr_blocks;
		for (unsigned i = 0; i < nr_blocks; i++) {
			if (new_s && old_s &&
			    !memcmp(&new_s->ptrs[i],
				    &old_s->ptrs[i],
				    sizeof(new_s->ptrs[i])))
				continue;

			if (new_s) {
				int ret = bch2_trans_mark_stripe_bucket(trans,
						bkey_s_c_to_stripe(new), i, false);
				if (ret)
					return ret;
			}

			if (old_s) {
				int ret = bch2_trans_mark_stripe_bucket(trans,
						bkey_s_c_to_stripe(old), i, true);
				if (ret)
					return ret;
			}
		}
	}

	if (!(flags & (BTREE_TRIGGER_TRANSACTIONAL|BTREE_TRIGGER_GC))) {
		struct stripe *m = genradix_ptr(&c->stripes, idx);

		if (!m) {
			struct printbuf buf1 = PRINTBUF;
			struct printbuf buf2 = PRINTBUF;

			bch2_bkey_val_to_text(&buf1, c, old);
			bch2_bkey_val_to_text(&buf2, c, new);
			bch_err_ratelimited(c, "error marking nonexistent stripe %llu while marking\n"
					    "old %s\n"
					    "new %s", idx, buf1.buf, buf2.buf);
			printbuf_exit(&buf2);
			printbuf_exit(&buf1);
			bch2_inconsistent_error(c);
			return -1;
		}

		if (!new_s) {
			bch2_stripes_heap_del(c, m, idx);

			memset(m, 0, sizeof(*m));
		} else {
			m->sectors	= le16_to_cpu(new_s->sectors);
			m->algorithm	= new_s->algorithm;
			m->nr_blocks	= new_s->nr_blocks;
			m->nr_redundant	= new_s->nr_redundant;
			m->blocks_nonempty = 0;

			for (unsigned i = 0; i < new_s->nr_blocks; i++)
				m->blocks_nonempty += !!stripe_blockcount_get(new_s, i);

			if (!old_s)
				bch2_stripes_heap_insert(c, m, idx);
			else
				bch2_stripes_heap_update(c, m, idx);
		}
	}

	if (flags & BTREE_TRIGGER_GC) {
		struct gc_stripe *m =
			genradix_ptr_alloc(&c->gc_stripes, idx, GFP_KERNEL);

		if (!m) {
			bch_err(c, "error allocating memory for gc_stripes, idx %llu",
				idx);
			return -BCH_ERR_ENOMEM_mark_stripe;
		}
		/*
		 * This will be wrong when we bring back runtime gc: we should
		 * be unmarking the old key and then marking the new key
		 */
		m->alive	= true;
		m->sectors	= le16_to_cpu(new_s->sectors);
		m->nr_blocks	= new_s->nr_blocks;
		m->nr_redundant	= new_s->nr_redundant;

		for (unsigned i = 0; i < new_s->nr_blocks; i++)
			m->ptrs[i] = new_s->ptrs[i];

		bch2_bkey_to_replicas(&m->r.e, new);

		/*
		 * gc recalculates this field from stripe ptr
		 * references:
		 */
		memset(m->block_sectors, 0, sizeof(m->block_sectors));

		for (unsigned i = 0; i < new_s->nr_blocks; i++) {
			int ret = mark_stripe_bucket(trans, new, i, flags);
			if (ret)
				return ret;
		}

		int ret = bch2_update_replicas(c, new, &m->r.e,
				      ((s64) m->sectors * m->nr_redundant),
				      0, true);
		if (ret) {
			struct printbuf buf = PRINTBUF;

			bch2_bkey_val_to_text(&buf, c, new);
			bch2_fs_fatal_error(c, "no replicas entry for %s", buf.buf);
			printbuf_exit(&buf);
			return ret;
		}
	}

	return 0;
}

/* returns blocknr in stripe that we matched: */
static const struct bch_extent_ptr *bkey_matches_stripe(struct bch_stripe *s,
						struct bkey_s_c k, unsigned *block)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	unsigned i, nr_data = s->nr_blocks - s->nr_redundant;

	bkey_for_each_ptr(ptrs, ptr)
		for (i = 0; i < nr_data; i++)
			if (__bch2_ptr_matches_stripe(&s->ptrs[i], ptr,
						      le16_to_cpu(s->sectors))) {
				*block = i;
				return ptr;
			}

	return NULL;
}

static bool extent_has_stripe_ptr(struct bkey_s_c k, u64 idx)
{
	switch (k.k->type) {
	case KEY_TYPE_extent: {
		struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
		const union bch_extent_entry *entry;

		extent_for_each_entry(e, entry)
			if (extent_entry_type(entry) ==
			    BCH_EXTENT_ENTRY_stripe_ptr &&
			    entry->stripe_ptr.idx == idx)
				return true;

		break;
	}
	}

	return false;
}

/* Stripe bufs: */

static void ec_stripe_buf_exit(struct ec_stripe_buf *buf)
{
	if (buf->key.k.type == KEY_TYPE_stripe) {
		struct bkey_i_stripe *s = bkey_i_to_stripe(&buf->key);
		unsigned i;

		for (i = 0; i < s->v.nr_blocks; i++) {
			kvpfree(buf->data[i], buf->size << 9);
			buf->data[i] = NULL;
		}
	}
}

/* XXX: this is a non-mempoolified memory allocation: */
static int ec_stripe_buf_init(struct ec_stripe_buf *buf,
			      unsigned offset, unsigned size)
{
	struct bch_stripe *v = &bkey_i_to_stripe(&buf->key)->v;
	unsigned csum_granularity = 1U << v->csum_granularity_bits;
	unsigned end = offset + size;
	unsigned i;

	BUG_ON(end > le16_to_cpu(v->sectors));

	offset	= round_down(offset, csum_granularity);
	end	= min_t(unsigned, le16_to_cpu(v->sectors),
			round_up(end, csum_granularity));

	buf->offset	= offset;
	buf->size	= end - offset;

	memset(buf->valid, 0xFF, sizeof(buf->valid));

	for (i = 0; i < v->nr_blocks; i++) {
		buf->data[i] = kvpmalloc(buf->size << 9, GFP_KERNEL);
		if (!buf->data[i])
			goto err;
	}

	return 0;
err:
	ec_stripe_buf_exit(buf);
	return -BCH_ERR_ENOMEM_stripe_buf;
}

/* Checksumming: */

static struct bch_csum ec_block_checksum(struct ec_stripe_buf *buf,
					 unsigned block, unsigned offset)
{
	struct bch_stripe *v = &bkey_i_to_stripe(&buf->key)->v;
	unsigned csum_granularity = 1 << v->csum_granularity_bits;
	unsigned end = buf->offset + buf->size;
	unsigned len = min(csum_granularity, end - offset);

	BUG_ON(offset >= end);
	BUG_ON(offset <  buf->offset);
	BUG_ON(offset & (csum_granularity - 1));
	BUG_ON(offset + len != le16_to_cpu(v->sectors) &&
	       (len & (csum_granularity - 1)));

	return bch2_checksum(NULL, v->csum_type,
			     null_nonce(),
			     buf->data[block] + ((offset - buf->offset) << 9),
			     len << 9);
}

static void ec_generate_checksums(struct ec_stripe_buf *buf)
{
	struct bch_stripe *v = &bkey_i_to_stripe(&buf->key)->v;
	unsigned i, j, csums_per_device = stripe_csums_per_device(v);

	if (!v->csum_type)
		return;

	BUG_ON(buf->offset);
	BUG_ON(buf->size != le16_to_cpu(v->sectors));

	for (i = 0; i < v->nr_blocks; i++)
		for (j = 0; j < csums_per_device; j++)
			stripe_csum_set(v, i, j,
				ec_block_checksum(buf, i, j << v->csum_granularity_bits));
}

static void ec_validate_checksums(struct bch_fs *c, struct ec_stripe_buf *buf)
{
	struct bch_stripe *v = &bkey_i_to_stripe(&buf->key)->v;
	unsigned csum_granularity = 1 << v->csum_granularity_bits;
	unsigned i;

	if (!v->csum_type)
		return;

	for (i = 0; i < v->nr_blocks; i++) {
		unsigned offset = buf->offset;
		unsigned end = buf->offset + buf->size;

		if (!test_bit(i, buf->valid))
			continue;

		while (offset < end) {
			unsigned j = offset >> v->csum_granularity_bits;
			unsigned len = min(csum_granularity, end - offset);
			struct bch_csum want = stripe_csum_get(v, i, j);
			struct bch_csum got = ec_block_checksum(buf, i, offset);

			if (bch2_crc_cmp(want, got)) {
				struct printbuf err = PRINTBUF;
				struct bch_dev *ca = bch_dev_bkey_exists(c, v->ptrs[i].dev);

				prt_printf(&err, "stripe checksum error: expected %0llx:%0llx got %0llx:%0llx (type %s)\n",
					   want.hi, want.lo,
					   got.hi, got.lo,
					   bch2_csum_types[v->csum_type]);
				prt_printf(&err, "  for %ps at %u of\n  ", (void *) _RET_IP_, i);
				bch2_bkey_val_to_text(&err, c, bkey_i_to_s_c(&buf->key));
				bch_err_ratelimited(ca, "%s", err.buf);
				printbuf_exit(&err);

				clear_bit(i, buf->valid);

				bch2_io_error(ca, BCH_MEMBER_ERROR_checksum);
				break;
			}

			offset += len;
		}
	}
}

/* Erasure coding: */

static void ec_generate_ec(struct ec_stripe_buf *buf)
{
	struct bch_stripe *v = &bkey_i_to_stripe(&buf->key)->v;
	unsigned nr_data = v->nr_blocks - v->nr_redundant;
	unsigned bytes = le16_to_cpu(v->sectors) << 9;

	raid_gen(nr_data, v->nr_redundant, bytes, buf->data);
}

static unsigned ec_nr_failed(struct ec_stripe_buf *buf)
{
	struct bch_stripe *v = &bkey_i_to_stripe(&buf->key)->v;

	return v->nr_blocks - bitmap_weight(buf->valid, v->nr_blocks);
}

static int ec_do_recov(struct bch_fs *c, struct ec_stripe_buf *buf)
{
	struct bch_stripe *v = &bkey_i_to_stripe(&buf->key)->v;
	unsigned i, failed[BCH_BKEY_PTRS_MAX], nr_failed = 0;
	unsigned nr_data = v->nr_blocks - v->nr_redundant;
	unsigned bytes = buf->size << 9;

	if (ec_nr_failed(buf) > v->nr_redundant) {
		bch_err_ratelimited(c,
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
	struct bch_stripe *v = &bkey_i_to_stripe(&ec_bio->buf->key)->v;
	struct bch_extent_ptr *ptr = &v->ptrs[ec_bio->idx];
	struct bch_dev *ca = ec_bio->ca;
	struct closure *cl = bio->bi_private;

	if (bch2_dev_io_err_on(bio->bi_status, ca,
			       bio_data_dir(bio)
			       ? BCH_MEMBER_ERROR_write
			       : BCH_MEMBER_ERROR_read,
			       "erasure coding %s error: %s",
			       bio_data_dir(bio) ? "write" : "read",
			       bch2_blk_status_to_str(bio->bi_status)))
		clear_bit(ec_bio->idx, ec_bio->buf->valid);

	if (ptr_stale(ca, ptr)) {
		bch_err_ratelimited(ca->fs,
				    "error %s stripe: stale pointer after io",
				    bio_data_dir(bio) == READ ? "reading from" : "writing to");
		clear_bit(ec_bio->idx, ec_bio->buf->valid);
	}

	bio_put(&ec_bio->bio);
	percpu_ref_put(&ca->io_ref);
	closure_put(cl);
}

static void ec_block_io(struct bch_fs *c, struct ec_stripe_buf *buf,
			blk_opf_t opf, unsigned idx, struct closure *cl)
{
	struct bch_stripe *v = &bkey_i_to_stripe(&buf->key)->v;
	unsigned offset = 0, bytes = buf->size << 9;
	struct bch_extent_ptr *ptr = &v->ptrs[idx];
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
	enum bch_data_type data_type = idx < v->nr_blocks - v->nr_redundant
		? BCH_DATA_user
		: BCH_DATA_parity;
	int rw = op_is_write(opf);

	if (ptr_stale(ca, ptr)) {
		bch_err_ratelimited(c,
				    "error %s stripe: stale pointer",
				    rw == READ ? "reading from" : "writing to");
		clear_bit(idx, buf->valid);
		return;
	}

	if (!bch2_dev_get_ioref(ca, rw)) {
		clear_bit(idx, buf->valid);
		return;
	}

	this_cpu_add(ca->io_done->sectors[rw][data_type], buf->size);

	while (offset < bytes) {
		unsigned nr_iovecs = min_t(size_t, BIO_MAX_VECS,
					   DIV_ROUND_UP(bytes, PAGE_SIZE));
		unsigned b = min_t(size_t, bytes - offset,
				   nr_iovecs << PAGE_SHIFT);
		struct ec_bio *ec_bio;

		ec_bio = container_of(bio_alloc_bioset(ca->disk_sb.bdev,
						       nr_iovecs,
						       opf,
						       GFP_KERNEL,
						       &c->ec_bioset),
				      struct ec_bio, bio);

		ec_bio->ca			= ca;
		ec_bio->buf			= buf;
		ec_bio->idx			= idx;

		ec_bio->bio.bi_iter.bi_sector	= ptr->offset + buf->offset + (offset >> 9);
		ec_bio->bio.bi_end_io		= ec_block_endio;
		ec_bio->bio.bi_private		= cl;

		bch2_bio_map(&ec_bio->bio, buf->data[idx] + offset, b);

		closure_get(cl);
		percpu_ref_get(&ca->io_ref);

		submit_bio(&ec_bio->bio);

		offset += b;
	}

	percpu_ref_put(&ca->io_ref);
}

static int get_stripe_key_trans(struct btree_trans *trans, u64 idx,
				struct ec_stripe_buf *stripe)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_stripes,
			       POS(0, idx), BTREE_ITER_SLOTS);
	ret = bkey_err(k);
	if (ret)
		goto err;
	if (k.k->type != KEY_TYPE_stripe) {
		ret = -ENOENT;
		goto err;
	}
	bkey_reassemble(&stripe->key, k);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/* recovery read path: */
int bch2_ec_read_extent(struct btree_trans *trans, struct bch_read_bio *rbio)
{
	struct bch_fs *c = trans->c;
	struct ec_stripe_buf *buf;
	struct closure cl;
	struct bch_stripe *v;
	unsigned i, offset;
	int ret = 0;

	closure_init_stack(&cl);

	BUG_ON(!rbio->pick.has_ec);

	buf = kzalloc(sizeof(*buf), GFP_NOFS);
	if (!buf)
		return -BCH_ERR_ENOMEM_ec_read_extent;

	ret = lockrestart_do(trans, get_stripe_key_trans(trans, rbio->pick.ec.idx, buf));
	if (ret) {
		bch_err_ratelimited(c,
			"error doing reconstruct read: error %i looking up stripe", ret);
		kfree(buf);
		return -EIO;
	}

	v = &bkey_i_to_stripe(&buf->key)->v;

	if (!bch2_ptr_matches_stripe(v, rbio->pick)) {
		bch_err_ratelimited(c,
			"error doing reconstruct read: pointer doesn't match stripe");
		ret = -EIO;
		goto err;
	}

	offset = rbio->bio.bi_iter.bi_sector - v->ptrs[rbio->pick.ec.block].offset;
	if (offset + bio_sectors(&rbio->bio) > le16_to_cpu(v->sectors)) {
		bch_err_ratelimited(c,
			"error doing reconstruct read: read is bigger than stripe");
		ret = -EIO;
		goto err;
	}

	ret = ec_stripe_buf_init(buf, offset, bio_sectors(&rbio->bio));
	if (ret)
		goto err;

	for (i = 0; i < v->nr_blocks; i++)
		ec_block_io(c, buf, REQ_OP_READ, i, &cl);

	closure_sync(&cl);

	if (ec_nr_failed(buf) > v->nr_redundant) {
		bch_err_ratelimited(c,
			"error doing reconstruct read: unable to read enough blocks");
		ret = -EIO;
		goto err;
	}

	ec_validate_checksums(c, buf);

	ret = ec_do_recov(c, buf);
	if (ret)
		goto err;

	memcpy_to_bio(&rbio->bio, rbio->bio.bi_iter,
		      buf->data[rbio->pick.ec.block] + ((offset - buf->offset) << 9));
err:
	ec_stripe_buf_exit(buf);
	kfree(buf);
	return ret;
}

/* stripe bucket accounting: */

static int __ec_stripe_mem_alloc(struct bch_fs *c, size_t idx, gfp_t gfp)
{
	ec_stripes_heap n, *h = &c->ec_stripes_heap;

	if (idx >= h->size) {
		if (!init_heap(&n, max(1024UL, roundup_pow_of_two(idx + 1)), gfp))
			return -BCH_ERR_ENOMEM_ec_stripe_mem_alloc;

		mutex_lock(&c->ec_stripes_heap_lock);
		if (n.size > h->size) {
			memcpy(n.data, h->data, h->used * sizeof(h->data[0]));
			n.used = h->used;
			swap(*h, n);
		}
		mutex_unlock(&c->ec_stripes_heap_lock);

		free_heap(&n);
	}

	if (!genradix_ptr_alloc(&c->stripes, idx, gfp))
		return -BCH_ERR_ENOMEM_ec_stripe_mem_alloc;

	if (c->gc_pos.phase != GC_PHASE_NOT_RUNNING &&
	    !genradix_ptr_alloc(&c->gc_stripes, idx, gfp))
		return -BCH_ERR_ENOMEM_ec_stripe_mem_alloc;

	return 0;
}

static int ec_stripe_mem_alloc(struct btree_trans *trans,
			       struct btree_iter *iter)
{
	return allocate_dropping_locks_errcode(trans,
			__ec_stripe_mem_alloc(trans->c, iter->pos.offset, _gfp));
}

/*
 * Hash table of open stripes:
 * Stripes that are being created or modified are kept in a hash table, so that
 * stripe deletion can skip them.
 */

static bool __bch2_stripe_is_open(struct bch_fs *c, u64 idx)
{
	unsigned hash = hash_64(idx, ilog2(ARRAY_SIZE(c->ec_stripes_new)));
	struct ec_stripe_new *s;

	hlist_for_each_entry(s, &c->ec_stripes_new[hash], hash)
		if (s->idx == idx)
			return true;
	return false;
}

static bool bch2_stripe_is_open(struct bch_fs *c, u64 idx)
{
	bool ret = false;

	spin_lock(&c->ec_stripes_new_lock);
	ret = __bch2_stripe_is_open(c, idx);
	spin_unlock(&c->ec_stripes_new_lock);

	return ret;
}

static bool bch2_try_open_stripe(struct bch_fs *c,
				 struct ec_stripe_new *s,
				 u64 idx)
{
	bool ret;

	spin_lock(&c->ec_stripes_new_lock);
	ret = !__bch2_stripe_is_open(c, idx);
	if (ret) {
		unsigned hash = hash_64(idx, ilog2(ARRAY_SIZE(c->ec_stripes_new)));

		s->idx = idx;
		hlist_add_head(&s->hash, &c->ec_stripes_new[hash]);
	}
	spin_unlock(&c->ec_stripes_new_lock);

	return ret;
}

static void bch2_stripe_close(struct bch_fs *c, struct ec_stripe_new *s)
{
	BUG_ON(!s->idx);

	spin_lock(&c->ec_stripes_new_lock);
	hlist_del_init(&s->hash);
	spin_unlock(&c->ec_stripes_new_lock);

	s->idx = 0;
}

/* Heap of all existing stripes, ordered by blocks_nonempty */

static u64 stripe_idx_to_delete(struct bch_fs *c)
{
	ec_stripes_heap *h = &c->ec_stripes_heap;

	lockdep_assert_held(&c->ec_stripes_heap_lock);

	if (h->used &&
	    h->data[0].blocks_nonempty == 0 &&
	    !bch2_stripe_is_open(c, h->data[0].idx))
		return h->data[0].idx;

	return 0;
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

	genradix_ptr(&c->stripes, h->data[i].idx)->heap_idx = i;
}

static void heap_verify_backpointer(struct bch_fs *c, size_t idx)
{
	ec_stripes_heap *h = &c->ec_stripes_heap;
	struct stripe *m = genradix_ptr(&c->stripes, idx);

	BUG_ON(m->heap_idx >= h->used);
	BUG_ON(h->data[m->heap_idx].idx != idx);
}

void bch2_stripes_heap_del(struct bch_fs *c,
			   struct stripe *m, size_t idx)
{
	mutex_lock(&c->ec_stripes_heap_lock);
	heap_verify_backpointer(c, idx);

	heap_del(&c->ec_stripes_heap, m->heap_idx,
		 ec_stripes_heap_cmp,
		 ec_stripes_heap_set_backpointer);
	mutex_unlock(&c->ec_stripes_heap_lock);
}

void bch2_stripes_heap_insert(struct bch_fs *c,
			      struct stripe *m, size_t idx)
{
	mutex_lock(&c->ec_stripes_heap_lock);
	BUG_ON(heap_full(&c->ec_stripes_heap));

	heap_add(&c->ec_stripes_heap, ((struct ec_stripe_heap_entry) {
			.idx = idx,
			.blocks_nonempty = m->blocks_nonempty,
		}),
		 ec_stripes_heap_cmp,
		 ec_stripes_heap_set_backpointer);

	heap_verify_backpointer(c, idx);
	mutex_unlock(&c->ec_stripes_heap_lock);
}

void bch2_stripes_heap_update(struct bch_fs *c,
			      struct stripe *m, size_t idx)
{
	ec_stripes_heap *h = &c->ec_stripes_heap;
	bool do_deletes;
	size_t i;

	mutex_lock(&c->ec_stripes_heap_lock);
	heap_verify_backpointer(c, idx);

	h->data[m->heap_idx].blocks_nonempty = m->blocks_nonempty;

	i = m->heap_idx;
	heap_sift_up(h,	  i, ec_stripes_heap_cmp,
		     ec_stripes_heap_set_backpointer);
	heap_sift_down(h, i, ec_stripes_heap_cmp,
		       ec_stripes_heap_set_backpointer);

	heap_verify_backpointer(c, idx);

	do_deletes = stripe_idx_to_delete(c) != 0;
	mutex_unlock(&c->ec_stripes_heap_lock);

	if (do_deletes)
		bch2_do_stripe_deletes(c);
}

/* stripe deletion */

static int ec_stripe_delete(struct btree_trans *trans, u64 idx)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_s_c_stripe s;
	int ret;

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_stripes, POS(0, idx),
			       BTREE_ITER_INTENT);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (k.k->type != KEY_TYPE_stripe) {
		bch2_fs_inconsistent(c, "attempting to delete nonexistent stripe %llu", idx);
		ret = -EINVAL;
		goto err;
	}

	s = bkey_s_c_to_stripe(k);
	for (unsigned i = 0; i < s.v->nr_blocks; i++)
		if (stripe_blockcount_get(s.v, i)) {
			struct printbuf buf = PRINTBUF;

			bch2_bkey_val_to_text(&buf, c, k);
			bch2_fs_inconsistent(c, "attempting to delete nonempty stripe %s", buf.buf);
			printbuf_exit(&buf);
			ret = -EINVAL;
			goto err;
		}

	ret = bch2_btree_delete_at(trans, &iter, 0);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static void ec_stripe_delete_work(struct work_struct *work)
{
	struct bch_fs *c =
		container_of(work, struct bch_fs, ec_stripe_delete_work);

	while (1) {
		mutex_lock(&c->ec_stripes_heap_lock);
		u64 idx = stripe_idx_to_delete(c);
		mutex_unlock(&c->ec_stripes_heap_lock);

		if (!idx)
			break;

		int ret = bch2_trans_do(c, NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
					ec_stripe_delete(trans, idx));
		bch_err_fn(c, ret);
		if (ret)
			break;
	}

	bch2_write_ref_put(c, BCH_WRITE_REF_stripe_delete);
}

void bch2_do_stripe_deletes(struct bch_fs *c)
{
	if (bch2_write_ref_tryget(c, BCH_WRITE_REF_stripe_delete) &&
	    !queue_work(c->write_ref_wq, &c->ec_stripe_delete_work))
		bch2_write_ref_put(c, BCH_WRITE_REF_stripe_delete);
}

/* stripe creation: */

static int ec_stripe_key_update(struct btree_trans *trans,
				struct bkey_i_stripe *new,
				bool create)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_stripes,
			       new->k.p, BTREE_ITER_INTENT);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (k.k->type != (create ? KEY_TYPE_deleted : KEY_TYPE_stripe)) {
		bch2_fs_inconsistent(c, "error %s stripe: got existing key type %s",
				     create ? "creating" : "updating",
				     bch2_bkey_types[k.k->type]);
		ret = -EINVAL;
		goto err;
	}

	if (k.k->type == KEY_TYPE_stripe) {
		const struct bch_stripe *old = bkey_s_c_to_stripe(k).v;
		unsigned i;

		if (old->nr_blocks != new->v.nr_blocks) {
			bch_err(c, "error updating stripe: nr_blocks does not match");
			ret = -EINVAL;
			goto err;
		}

		for (i = 0; i < new->v.nr_blocks; i++) {
			unsigned v = stripe_blockcount_get(old, i);

			BUG_ON(v &&
			       (old->ptrs[i].dev != new->v.ptrs[i].dev ||
				old->ptrs[i].gen != new->v.ptrs[i].gen ||
				old->ptrs[i].offset != new->v.ptrs[i].offset));

			stripe_blockcount_set(&new->v, i, v);
		}
	}

	ret = bch2_trans_update(trans, &iter, &new->k_i, 0);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int ec_stripe_update_extent(struct btree_trans *trans,
				   struct bpos bucket, u8 gen,
				   struct ec_stripe_buf *s,
				   struct bpos *bp_pos)
{
	struct bch_stripe *v = &bkey_i_to_stripe(&s->key)->v;
	struct bch_fs *c = trans->c;
	struct bch_backpointer bp;
	struct btree_iter iter;
	struct bkey_s_c k;
	const struct bch_extent_ptr *ptr_c;
	struct bch_extent_ptr *ptr, *ec_ptr = NULL;
	struct bch_extent_stripe_ptr stripe_ptr;
	struct bkey_i *n;
	int ret, dev, block;

	ret = bch2_get_next_backpointer(trans, bucket, gen,
				bp_pos, &bp, BTREE_ITER_CACHED);
	if (ret)
		return ret;
	if (bpos_eq(*bp_pos, SPOS_MAX))
		return 0;

	if (bp.level) {
		struct printbuf buf = PRINTBUF;
		struct btree_iter node_iter;
		struct btree *b;

		b = bch2_backpointer_get_node(trans, &node_iter, *bp_pos, bp);
		bch2_trans_iter_exit(trans, &node_iter);

		if (!b)
			return 0;

		prt_printf(&buf, "found btree node in erasure coded bucket: b=%px\n", b);
		bch2_backpointer_to_text(&buf, &bp);

		bch2_fs_inconsistent(c, "%s", buf.buf);
		printbuf_exit(&buf);
		return -EIO;
	}

	k = bch2_backpointer_get_key(trans, &iter, *bp_pos, bp, BTREE_ITER_INTENT);
	ret = bkey_err(k);
	if (ret)
		return ret;
	if (!k.k) {
		/*
		 * extent no longer exists - we could flush the btree
		 * write buffer and retry to verify, but no need:
		 */
		return 0;
	}

	if (extent_has_stripe_ptr(k, s->key.k.p.offset))
		goto out;

	ptr_c = bkey_matches_stripe(v, k, &block);
	/*
	 * It doesn't generally make sense to erasure code cached ptrs:
	 * XXX: should we be incrementing a counter?
	 */
	if (!ptr_c || ptr_c->cached)
		goto out;

	dev = v->ptrs[block].dev;

	n = bch2_trans_kmalloc(trans, bkey_bytes(k.k) + sizeof(stripe_ptr));
	ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		goto out;

	bkey_reassemble(n, k);

	bch2_bkey_drop_ptrs(bkey_i_to_s(n), ptr, ptr->dev != dev);
	ec_ptr = bch2_bkey_has_device(bkey_i_to_s(n), dev);
	BUG_ON(!ec_ptr);

	stripe_ptr = (struct bch_extent_stripe_ptr) {
		.type = 1 << BCH_EXTENT_ENTRY_stripe_ptr,
		.block		= block,
		.redundancy	= v->nr_redundant,
		.idx		= s->key.k.p.offset,
	};

	__extent_entry_insert(n,
			(union bch_extent_entry *) ec_ptr,
			(union bch_extent_entry *) &stripe_ptr);

	ret = bch2_trans_update(trans, &iter, n, 0);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int ec_stripe_update_bucket(struct btree_trans *trans, struct ec_stripe_buf *s,
				   unsigned block)
{
	struct bch_fs *c = trans->c;
	struct bch_stripe *v = &bkey_i_to_stripe(&s->key)->v;
	struct bch_extent_ptr bucket = v->ptrs[block];
	struct bpos bucket_pos = PTR_BUCKET_POS(c, &bucket);
	struct bpos bp_pos = POS_MIN;
	int ret = 0;

	while (1) {
		ret = commit_do(trans, NULL, NULL,
				BCH_TRANS_COMMIT_no_check_rw|
				BCH_TRANS_COMMIT_no_enospc,
			ec_stripe_update_extent(trans, bucket_pos, bucket.gen,
						s, &bp_pos));
		if (ret)
			break;
		if (bkey_eq(bp_pos, POS_MAX))
			break;

		bp_pos = bpos_nosnap_successor(bp_pos);
	}

	return ret;
}

static int ec_stripe_update_extents(struct bch_fs *c, struct ec_stripe_buf *s)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct bch_stripe *v = &bkey_i_to_stripe(&s->key)->v;
	unsigned i, nr_data = v->nr_blocks - v->nr_redundant;
	int ret = 0;

	ret = bch2_btree_write_buffer_flush_sync(trans);
	if (ret)
		goto err;

	for (i = 0; i < nr_data; i++) {
		ret = ec_stripe_update_bucket(trans, s, i);
		if (ret)
			break;
	}
err:
	bch2_trans_put(trans);

	return ret;
}

static void zero_out_rest_of_ec_bucket(struct bch_fs *c,
				       struct ec_stripe_new *s,
				       unsigned block,
				       struct open_bucket *ob)
{
	struct bch_dev *ca = bch_dev_bkey_exists(c, ob->dev);
	unsigned offset = ca->mi.bucket_size - ob->sectors_free;
	int ret;

	if (!bch2_dev_get_ioref(ca, WRITE)) {
		s->err = -BCH_ERR_erofs_no_writes;
		return;
	}

	memset(s->new_stripe.data[block] + (offset << 9),
	       0,
	       ob->sectors_free << 9);

	ret = blkdev_issue_zeroout(ca->disk_sb.bdev,
			ob->bucket * ca->mi.bucket_size + offset,
			ob->sectors_free,
			GFP_KERNEL, 0);

	percpu_ref_put(&ca->io_ref);

	if (ret)
		s->err = ret;
}

void bch2_ec_stripe_new_free(struct bch_fs *c, struct ec_stripe_new *s)
{
	if (s->idx)
		bch2_stripe_close(c, s);
	kfree(s);
}

/*
 * data buckets of new stripe all written: create the stripe
 */
static void ec_stripe_create(struct ec_stripe_new *s)
{
	struct bch_fs *c = s->c;
	struct open_bucket *ob;
	struct bch_stripe *v = &bkey_i_to_stripe(&s->new_stripe.key)->v;
	unsigned i, nr_data = v->nr_blocks - v->nr_redundant;
	int ret;

	BUG_ON(s->h->s == s);

	closure_sync(&s->iodone);

	if (!s->err) {
		for (i = 0; i < nr_data; i++)
			if (s->blocks[i]) {
				ob = c->open_buckets + s->blocks[i];

				if (ob->sectors_free)
					zero_out_rest_of_ec_bucket(c, s, i, ob);
			}
	}

	if (s->err) {
		if (!bch2_err_matches(s->err, EROFS))
			bch_err(c, "error creating stripe: error writing data buckets");
		goto err;
	}

	if (s->have_existing_stripe) {
		ec_validate_checksums(c, &s->existing_stripe);

		if (ec_do_recov(c, &s->existing_stripe)) {
			bch_err(c, "error creating stripe: error reading existing stripe");
			goto err;
		}

		for (i = 0; i < nr_data; i++)
			if (stripe_blockcount_get(&bkey_i_to_stripe(&s->existing_stripe.key)->v, i))
				swap(s->new_stripe.data[i],
				     s->existing_stripe.data[i]);

		ec_stripe_buf_exit(&s->existing_stripe);
	}

	BUG_ON(!s->allocated);
	BUG_ON(!s->idx);

	ec_generate_ec(&s->new_stripe);

	ec_generate_checksums(&s->new_stripe);

	/* write p/q: */
	for (i = nr_data; i < v->nr_blocks; i++)
		ec_block_io(c, &s->new_stripe, REQ_OP_WRITE, i, &s->iodone);
	closure_sync(&s->iodone);

	if (ec_nr_failed(&s->new_stripe)) {
		bch_err(c, "error creating stripe: error writing redundancy buckets");
		goto err;
	}

	ret = bch2_trans_do(c, &s->res, NULL,
			    BCH_TRANS_COMMIT_no_check_rw|
			    BCH_TRANS_COMMIT_no_enospc,
			    ec_stripe_key_update(trans,
					bkey_i_to_stripe(&s->new_stripe.key),
					!s->have_existing_stripe));
	bch_err_msg(c, ret, "creating stripe key");
	if (ret) {
		goto err;
	}

	ret = ec_stripe_update_extents(c, &s->new_stripe);
	bch_err_msg(c, ret, "error updating extents");
	if (ret)
		goto err;
err:
	bch2_disk_reservation_put(c, &s->res);

	for (i = 0; i < v->nr_blocks; i++)
		if (s->blocks[i]) {
			ob = c->open_buckets + s->blocks[i];

			if (i < nr_data) {
				ob->ec = NULL;
				__bch2_open_bucket_put(c, ob);
			} else {
				bch2_open_bucket_put(c, ob);
			}
		}

	mutex_lock(&c->ec_stripe_new_lock);
	list_del(&s->list);
	mutex_unlock(&c->ec_stripe_new_lock);
	wake_up(&c->ec_stripe_new_wait);

	ec_stripe_buf_exit(&s->existing_stripe);
	ec_stripe_buf_exit(&s->new_stripe);
	closure_debug_destroy(&s->iodone);

	ec_stripe_new_put(c, s, STRIPE_REF_stripe);
}

static struct ec_stripe_new *get_pending_stripe(struct bch_fs *c)
{
	struct ec_stripe_new *s;

	mutex_lock(&c->ec_stripe_new_lock);
	list_for_each_entry(s, &c->ec_stripe_new_list, list)
		if (!atomic_read(&s->ref[STRIPE_REF_io]))
			goto out;
	s = NULL;
out:
	mutex_unlock(&c->ec_stripe_new_lock);

	return s;
}

static void ec_stripe_create_work(struct work_struct *work)
{
	struct bch_fs *c = container_of(work,
		struct bch_fs, ec_stripe_create_work);
	struct ec_stripe_new *s;

	while ((s = get_pending_stripe(c)))
		ec_stripe_create(s);

	bch2_write_ref_put(c, BCH_WRITE_REF_stripe_create);
}

void bch2_ec_do_stripe_creates(struct bch_fs *c)
{
	bch2_write_ref_get(c, BCH_WRITE_REF_stripe_create);

	if (!queue_work(system_long_wq, &c->ec_stripe_create_work))
		bch2_write_ref_put(c, BCH_WRITE_REF_stripe_create);
}

static void ec_stripe_set_pending(struct bch_fs *c, struct ec_stripe_head *h)
{
	struct ec_stripe_new *s = h->s;

	BUG_ON(!s->allocated && !s->err);

	h->s		= NULL;
	s->pending	= true;

	mutex_lock(&c->ec_stripe_new_lock);
	list_add(&s->list, &c->ec_stripe_new_list);
	mutex_unlock(&c->ec_stripe_new_lock);

	ec_stripe_new_put(c, s, STRIPE_REF_io);
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

	BUG_ON(!ob->ec->new_stripe.data[ob->ec_idx]);

	ca	= bch_dev_bkey_exists(c, ob->dev);
	offset	= ca->mi.bucket_size - ob->sectors_free;

	return ob->ec->new_stripe.data[ob->ec_idx] + (offset << 9);
}

static int unsigned_cmp(const void *_l, const void *_r)
{
	unsigned l = *((const unsigned *) _l);
	unsigned r = *((const unsigned *) _r);

	return cmp_int(l, r);
}

/* pick most common bucket size: */
static unsigned pick_blocksize(struct bch_fs *c,
			       struct bch_devs_mask *devs)
{
	unsigned nr = 0, sizes[BCH_SB_MEMBERS_MAX];
	struct {
		unsigned nr, size;
	} cur = { 0, 0 }, best = { 0, 0 };

	for_each_member_device_rcu(c, ca, devs)
		sizes[nr++] = ca->mi.bucket_size;

	sort(sizes, nr, sizeof(unsigned), unsigned_cmp, NULL);

	for (unsigned i = 0; i < nr; i++) {
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

static bool may_create_new_stripe(struct bch_fs *c)
{
	return false;
}

static void ec_stripe_key_init(struct bch_fs *c,
			       struct bkey_i *k,
			       unsigned nr_data,
			       unsigned nr_parity,
			       unsigned stripe_size)
{
	struct bkey_i_stripe *s = bkey_stripe_init(k);
	unsigned u64s;

	s->v.sectors			= cpu_to_le16(stripe_size);
	s->v.algorithm			= 0;
	s->v.nr_blocks			= nr_data + nr_parity;
	s->v.nr_redundant		= nr_parity;
	s->v.csum_granularity_bits	= ilog2(c->opts.encoded_extent_max >> 9);
	s->v.csum_type			= BCH_CSUM_crc32c;
	s->v.pad			= 0;

	while ((u64s = stripe_val_u64s(&s->v)) > BKEY_VAL_U64s_MAX) {
		BUG_ON(1 << s->v.csum_granularity_bits >=
		       le16_to_cpu(s->v.sectors) ||
		       s->v.csum_granularity_bits == U8_MAX);
		s->v.csum_granularity_bits++;
	}

	set_bkey_val_u64s(&s->k, u64s);
}

static int ec_new_stripe_alloc(struct bch_fs *c, struct ec_stripe_head *h)
{
	struct ec_stripe_new *s;

	lockdep_assert_held(&h->lock);

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -BCH_ERR_ENOMEM_ec_new_stripe_alloc;

	mutex_init(&s->lock);
	closure_init(&s->iodone, NULL);
	atomic_set(&s->ref[STRIPE_REF_stripe], 1);
	atomic_set(&s->ref[STRIPE_REF_io], 1);
	s->c		= c;
	s->h		= h;
	s->nr_data	= min_t(unsigned, h->nr_active_devs,
				BCH_BKEY_PTRS_MAX) - h->redundancy;
	s->nr_parity	= h->redundancy;

	ec_stripe_key_init(c, &s->new_stripe.key,
			   s->nr_data, s->nr_parity, h->blocksize);

	h->s = s;
	return 0;
}

static struct ec_stripe_head *
ec_new_stripe_head_alloc(struct bch_fs *c, unsigned target,
			 unsigned algo, unsigned redundancy,
			 enum bch_watermark watermark)
{
	struct ec_stripe_head *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return NULL;

	mutex_init(&h->lock);
	BUG_ON(!mutex_trylock(&h->lock));

	h->target	= target;
	h->algo		= algo;
	h->redundancy	= redundancy;
	h->watermark	= watermark;

	rcu_read_lock();
	h->devs = target_rw_devs(c, BCH_DATA_user, target);

	for_each_member_device_rcu(c, ca, &h->devs)
		if (!ca->mi.durability)
			__clear_bit(ca->dev_idx, h->devs.d);

	h->blocksize = pick_blocksize(c, &h->devs);

	for_each_member_device_rcu(c, ca, &h->devs)
		if (ca->mi.bucket_size == h->blocksize)
			h->nr_active_devs++;

	rcu_read_unlock();

	/*
	 * If we only have redundancy + 1 devices, we're better off with just
	 * replication:
	 */
	if (h->nr_active_devs < h->redundancy + 2)
		bch_err(c, "insufficient devices available to create stripe (have %u, need %u) - mismatched bucket sizes?",
			h->nr_active_devs, h->redundancy + 2);

	list_add(&h->list, &c->ec_stripe_head_list);
	return h;
}

void bch2_ec_stripe_head_put(struct bch_fs *c, struct ec_stripe_head *h)
{
	if (h->s &&
	    h->s->allocated &&
	    bitmap_weight(h->s->blocks_allocated,
			  h->s->nr_data) == h->s->nr_data)
		ec_stripe_set_pending(c, h);

	mutex_unlock(&h->lock);
}

static struct ec_stripe_head *
__bch2_ec_stripe_head_get(struct btree_trans *trans,
			  unsigned target,
			  unsigned algo,
			  unsigned redundancy,
			  enum bch_watermark watermark)
{
	struct bch_fs *c = trans->c;
	struct ec_stripe_head *h;
	int ret;

	if (!redundancy)
		return NULL;

	ret = bch2_trans_mutex_lock(trans, &c->ec_stripe_head_lock);
	if (ret)
		return ERR_PTR(ret);

	if (test_bit(BCH_FS_going_ro, &c->flags)) {
		h = ERR_PTR(-BCH_ERR_erofs_no_writes);
		goto found;
	}

	list_for_each_entry(h, &c->ec_stripe_head_list, list)
		if (h->target		== target &&
		    h->algo		== algo &&
		    h->redundancy	== redundancy &&
		    h->watermark	== watermark) {
			ret = bch2_trans_mutex_lock(trans, &h->lock);
			if (ret)
				h = ERR_PTR(ret);
			goto found;
		}

	h = ec_new_stripe_head_alloc(c, target, algo, redundancy, watermark);
found:
	if (!IS_ERR_OR_NULL(h) &&
	    h->nr_active_devs < h->redundancy + 2) {
		mutex_unlock(&h->lock);
		h = NULL;
	}
	mutex_unlock(&c->ec_stripe_head_lock);
	return h;
}

static int new_stripe_alloc_buckets(struct btree_trans *trans, struct ec_stripe_head *h,
				    enum bch_watermark watermark, struct closure *cl)
{
	struct bch_fs *c = trans->c;
	struct bch_devs_mask devs = h->devs;
	struct open_bucket *ob;
	struct open_buckets buckets;
	struct bch_stripe *v = &bkey_i_to_stripe(&h->s->new_stripe.key)->v;
	unsigned i, j, nr_have_parity = 0, nr_have_data = 0;
	bool have_cache = true;
	int ret = 0;

	BUG_ON(v->nr_blocks	!= h->s->nr_data + h->s->nr_parity);
	BUG_ON(v->nr_redundant	!= h->s->nr_parity);

	for_each_set_bit(i, h->s->blocks_gotten, v->nr_blocks) {
		__clear_bit(v->ptrs[i].dev, devs.d);
		if (i < h->s->nr_data)
			nr_have_data++;
		else
			nr_have_parity++;
	}

	BUG_ON(nr_have_data	> h->s->nr_data);
	BUG_ON(nr_have_parity	> h->s->nr_parity);

	buckets.nr = 0;
	if (nr_have_parity < h->s->nr_parity) {
		ret = bch2_bucket_alloc_set_trans(trans, &buckets,
					    &h->parity_stripe,
					    &devs,
					    h->s->nr_parity,
					    &nr_have_parity,
					    &have_cache, 0,
					    BCH_DATA_parity,
					    watermark,
					    cl);

		open_bucket_for_each(c, &buckets, ob, i) {
			j = find_next_zero_bit(h->s->blocks_gotten,
					       h->s->nr_data + h->s->nr_parity,
					       h->s->nr_data);
			BUG_ON(j >= h->s->nr_data + h->s->nr_parity);

			h->s->blocks[j] = buckets.v[i];
			v->ptrs[j] = bch2_ob_ptr(c, ob);
			__set_bit(j, h->s->blocks_gotten);
		}

		if (ret)
			return ret;
	}

	buckets.nr = 0;
	if (nr_have_data < h->s->nr_data) {
		ret = bch2_bucket_alloc_set_trans(trans, &buckets,
					    &h->block_stripe,
					    &devs,
					    h->s->nr_data,
					    &nr_have_data,
					    &have_cache, 0,
					    BCH_DATA_user,
					    watermark,
					    cl);

		open_bucket_for_each(c, &buckets, ob, i) {
			j = find_next_zero_bit(h->s->blocks_gotten,
					       h->s->nr_data, 0);
			BUG_ON(j >= h->s->nr_data);

			h->s->blocks[j] = buckets.v[i];
			v->ptrs[j] = bch2_ob_ptr(c, ob);
			__set_bit(j, h->s->blocks_gotten);
		}

		if (ret)
			return ret;
	}

	return 0;
}

/* XXX: doesn't obey target: */
static s64 get_existing_stripe(struct bch_fs *c,
			       struct ec_stripe_head *head)
{
	ec_stripes_heap *h = &c->ec_stripes_heap;
	struct stripe *m;
	size_t heap_idx;
	u64 stripe_idx;
	s64 ret = -1;

	if (may_create_new_stripe(c))
		return -1;

	mutex_lock(&c->ec_stripes_heap_lock);
	for (heap_idx = 0; heap_idx < h->used; heap_idx++) {
		/* No blocks worth reusing, stripe will just be deleted: */
		if (!h->data[heap_idx].blocks_nonempty)
			continue;

		stripe_idx = h->data[heap_idx].idx;

		m = genradix_ptr(&c->stripes, stripe_idx);

		if (m->algorithm	== head->algo &&
		    m->nr_redundant	== head->redundancy &&
		    m->sectors		== head->blocksize &&
		    m->blocks_nonempty	< m->nr_blocks - m->nr_redundant &&
		    bch2_try_open_stripe(c, head->s, stripe_idx)) {
			ret = stripe_idx;
			break;
		}
	}
	mutex_unlock(&c->ec_stripes_heap_lock);
	return ret;
}

static int __bch2_ec_stripe_head_reuse(struct btree_trans *trans, struct ec_stripe_head *h)
{
	struct bch_fs *c = trans->c;
	struct bch_stripe *new_v = &bkey_i_to_stripe(&h->s->new_stripe.key)->v;
	struct bch_stripe *existing_v;
	unsigned i;
	s64 idx;
	int ret;

	/*
	 * If we can't allocate a new stripe, and there's no stripes with empty
	 * blocks for us to reuse, that means we have to wait on copygc:
	 */
	idx = get_existing_stripe(c, h);
	if (idx < 0)
		return -BCH_ERR_stripe_alloc_blocked;

	ret = get_stripe_key_trans(trans, idx, &h->s->existing_stripe);
	if (ret) {
		bch2_stripe_close(c, h->s);
		if (!bch2_err_matches(ret, BCH_ERR_transaction_restart))
			bch2_fs_fatal_error(c, "error reading stripe key: %s", bch2_err_str(ret));
		return ret;
	}

	existing_v = &bkey_i_to_stripe(&h->s->existing_stripe.key)->v;

	BUG_ON(existing_v->nr_redundant != h->s->nr_parity);
	h->s->nr_data = existing_v->nr_blocks -
		existing_v->nr_redundant;

	ret = ec_stripe_buf_init(&h->s->existing_stripe, 0, h->blocksize);
	if (ret) {
		bch2_stripe_close(c, h->s);
		return ret;
	}

	BUG_ON(h->s->existing_stripe.size != h->blocksize);
	BUG_ON(h->s->existing_stripe.size != le16_to_cpu(existing_v->sectors));

	/*
	 * Free buckets we initially allocated - they might conflict with
	 * blocks from the stripe we're reusing:
	 */
	for_each_set_bit(i, h->s->blocks_gotten, new_v->nr_blocks) {
		bch2_open_bucket_put(c, c->open_buckets + h->s->blocks[i]);
		h->s->blocks[i] = 0;
	}
	memset(h->s->blocks_gotten, 0, sizeof(h->s->blocks_gotten));
	memset(h->s->blocks_allocated, 0, sizeof(h->s->blocks_allocated));

	for (i = 0; i < existing_v->nr_blocks; i++) {
		if (stripe_blockcount_get(existing_v, i)) {
			__set_bit(i, h->s->blocks_gotten);
			__set_bit(i, h->s->blocks_allocated);
		}

		ec_block_io(c, &h->s->existing_stripe, READ, i, &h->s->iodone);
	}

	bkey_copy(&h->s->new_stripe.key, &h->s->existing_stripe.key);
	h->s->have_existing_stripe = true;

	return 0;
}

static int __bch2_ec_stripe_head_reserve(struct btree_trans *trans, struct ec_stripe_head *h)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bpos min_pos = POS(0, 1);
	struct bpos start_pos = bpos_max(min_pos, POS(0, c->ec_stripe_hint));
	int ret;

	if (!h->s->res.sectors) {
		ret = bch2_disk_reservation_get(c, &h->s->res,
					h->blocksize,
					h->s->nr_parity,
					BCH_DISK_RESERVATION_NOFAIL);
		if (ret)
			return ret;
	}

	for_each_btree_key_norestart(trans, iter, BTREE_ID_stripes, start_pos,
			   BTREE_ITER_SLOTS|BTREE_ITER_INTENT, k, ret) {
		if (bkey_gt(k.k->p, POS(0, U32_MAX))) {
			if (start_pos.offset) {
				start_pos = min_pos;
				bch2_btree_iter_set_pos(&iter, start_pos);
				continue;
			}

			ret = -BCH_ERR_ENOSPC_stripe_create;
			break;
		}

		if (bkey_deleted(k.k) &&
		    bch2_try_open_stripe(c, h->s, k.k->p.offset))
			break;
	}

	c->ec_stripe_hint = iter.pos.offset;

	if (ret)
		goto err;

	ret = ec_stripe_mem_alloc(trans, &iter);
	if (ret) {
		bch2_stripe_close(c, h->s);
		goto err;
	}

	h->s->new_stripe.key.k.p = iter.pos;
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
err:
	bch2_disk_reservation_put(c, &h->s->res);
	goto out;
}

struct ec_stripe_head *bch2_ec_stripe_head_get(struct btree_trans *trans,
					       unsigned target,
					       unsigned algo,
					       unsigned redundancy,
					       enum bch_watermark watermark,
					       struct closure *cl)
{
	struct bch_fs *c = trans->c;
	struct ec_stripe_head *h;
	bool waiting = false;
	int ret;

	h = __bch2_ec_stripe_head_get(trans, target, algo, redundancy, watermark);
	if (IS_ERR_OR_NULL(h))
		return h;

	if (!h->s) {
		ret = ec_new_stripe_alloc(c, h);
		if (ret) {
			bch_err(c, "failed to allocate new stripe");
			goto err;
		}
	}

	if (h->s->allocated)
		goto allocated;

	if (h->s->have_existing_stripe)
		goto alloc_existing;

	/* First, try to allocate a full stripe: */
	ret =   new_stripe_alloc_buckets(trans, h, BCH_WATERMARK_stripe, NULL) ?:
		__bch2_ec_stripe_head_reserve(trans, h);
	if (!ret)
		goto allocate_buf;
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart) ||
	    bch2_err_matches(ret, ENOMEM))
		goto err;

	/*
	 * Not enough buckets available for a full stripe: we must reuse an
	 * existing stripe:
	 */
	while (1) {
		ret = __bch2_ec_stripe_head_reuse(trans, h);
		if (!ret)
			break;
		if (waiting || !cl || ret != -BCH_ERR_stripe_alloc_blocked)
			goto err;

		if (watermark == BCH_WATERMARK_copygc) {
			ret =   new_stripe_alloc_buckets(trans, h, watermark, NULL) ?:
				__bch2_ec_stripe_head_reserve(trans, h);
			if (ret)
				goto err;
			goto allocate_buf;
		}

		/* XXX freelist_wait? */
		closure_wait(&c->freelist_wait, cl);
		waiting = true;
	}

	if (waiting)
		closure_wake_up(&c->freelist_wait);
alloc_existing:
	/*
	 * Retry allocating buckets, with the watermark for this
	 * particular write:
	 */
	ret = new_stripe_alloc_buckets(trans, h, watermark, cl);
	if (ret)
		goto err;

allocate_buf:
	ret = ec_stripe_buf_init(&h->s->new_stripe, 0, h->blocksize);
	if (ret)
		goto err;

	h->s->allocated = true;
allocated:
	BUG_ON(!h->s->idx);
	BUG_ON(!h->s->new_stripe.data[0]);
	BUG_ON(trans->restarted);
	return h;
err:
	bch2_ec_stripe_head_put(c, h);
	return ERR_PTR(ret);
}

static void __bch2_ec_stop(struct bch_fs *c, struct bch_dev *ca)
{
	struct ec_stripe_head *h;
	struct open_bucket *ob;
	unsigned i;

	mutex_lock(&c->ec_stripe_head_lock);
	list_for_each_entry(h, &c->ec_stripe_head_list, list) {
		mutex_lock(&h->lock);
		if (!h->s)
			goto unlock;

		if (!ca)
			goto found;

		for (i = 0; i < bkey_i_to_stripe(&h->s->new_stripe.key)->v.nr_blocks; i++) {
			if (!h->s->blocks[i])
				continue;

			ob = c->open_buckets + h->s->blocks[i];
			if (ob->dev == ca->dev_idx)
				goto found;
		}
		goto unlock;
found:
		h->s->err = -BCH_ERR_erofs_no_writes;
		ec_stripe_set_pending(c, h);
unlock:
		mutex_unlock(&h->lock);
	}
	mutex_unlock(&c->ec_stripe_head_lock);
}

void bch2_ec_stop_dev(struct bch_fs *c, struct bch_dev *ca)
{
	__bch2_ec_stop(c, ca);
}

void bch2_fs_ec_stop(struct bch_fs *c)
{
	__bch2_ec_stop(c, NULL);
}

static bool bch2_fs_ec_flush_done(struct bch_fs *c)
{
	bool ret;

	mutex_lock(&c->ec_stripe_new_lock);
	ret = list_empty(&c->ec_stripe_new_list);
	mutex_unlock(&c->ec_stripe_new_lock);

	return ret;
}

void bch2_fs_ec_flush(struct bch_fs *c)
{
	wait_event(c->ec_stripe_new_wait, bch2_fs_ec_flush_done(c));
}

int bch2_stripes_read(struct bch_fs *c)
{
	int ret = bch2_trans_run(c,
		for_each_btree_key(trans, iter, BTREE_ID_stripes, POS_MIN,
				   BTREE_ITER_PREFETCH, k, ({
			if (k.k->type != KEY_TYPE_stripe)
				continue;

			ret = __ec_stripe_mem_alloc(c, k.k->p.offset, GFP_KERNEL);
			if (ret)
				break;

			const struct bch_stripe *s = bkey_s_c_to_stripe(k).v;

			struct stripe *m = genradix_ptr(&c->stripes, k.k->p.offset);
			m->sectors	= le16_to_cpu(s->sectors);
			m->algorithm	= s->algorithm;
			m->nr_blocks	= s->nr_blocks;
			m->nr_redundant	= s->nr_redundant;
			m->blocks_nonempty = 0;

			for (unsigned i = 0; i < s->nr_blocks; i++)
				m->blocks_nonempty += !!stripe_blockcount_get(s, i);

			bch2_stripes_heap_insert(c, m, k.k->p.offset);
			0;
		})));
	bch_err_fn(c, ret);
	return ret;
}

void bch2_stripes_heap_to_text(struct printbuf *out, struct bch_fs *c)
{
	ec_stripes_heap *h = &c->ec_stripes_heap;
	struct stripe *m;
	size_t i;

	mutex_lock(&c->ec_stripes_heap_lock);
	for (i = 0; i < min_t(size_t, h->used, 50); i++) {
		m = genradix_ptr(&c->stripes, h->data[i].idx);

		prt_printf(out, "%zu %u/%u+%u", h->data[i].idx,
		       h->data[i].blocks_nonempty,
		       m->nr_blocks - m->nr_redundant,
		       m->nr_redundant);
		if (bch2_stripe_is_open(c, h->data[i].idx))
			prt_str(out, " open");
		prt_newline(out);
	}
	mutex_unlock(&c->ec_stripes_heap_lock);
}

void bch2_new_stripes_to_text(struct printbuf *out, struct bch_fs *c)
{
	struct ec_stripe_head *h;
	struct ec_stripe_new *s;

	mutex_lock(&c->ec_stripe_head_lock);
	list_for_each_entry(h, &c->ec_stripe_head_list, list) {
		prt_printf(out, "target %u algo %u redundancy %u %s:\n",
		       h->target, h->algo, h->redundancy,
		       bch2_watermarks[h->watermark]);

		if (h->s)
			prt_printf(out, "\tidx %llu blocks %u+%u allocated %u\n",
			       h->s->idx, h->s->nr_data, h->s->nr_parity,
			       bitmap_weight(h->s->blocks_allocated,
					     h->s->nr_data));
	}
	mutex_unlock(&c->ec_stripe_head_lock);

	prt_printf(out, "in flight:\n");

	mutex_lock(&c->ec_stripe_new_lock);
	list_for_each_entry(s, &c->ec_stripe_new_list, list) {
		prt_printf(out, "\tidx %llu blocks %u+%u ref %u %u %s\n",
			   s->idx, s->nr_data, s->nr_parity,
			   atomic_read(&s->ref[STRIPE_REF_io]),
			   atomic_read(&s->ref[STRIPE_REF_stripe]),
			   bch2_watermarks[s->h->watermark]);
	}
	mutex_unlock(&c->ec_stripe_new_lock);
}

void bch2_fs_ec_exit(struct bch_fs *c)
{
	struct ec_stripe_head *h;
	unsigned i;

	while (1) {
		mutex_lock(&c->ec_stripe_head_lock);
		h = list_first_entry_or_null(&c->ec_stripe_head_list,
					     struct ec_stripe_head, list);
		if (h)
			list_del(&h->list);
		mutex_unlock(&c->ec_stripe_head_lock);
		if (!h)
			break;

		if (h->s) {
			for (i = 0; i < bkey_i_to_stripe(&h->s->new_stripe.key)->v.nr_blocks; i++)
				BUG_ON(h->s->blocks[i]);

			kfree(h->s);
		}
		kfree(h);
	}

	BUG_ON(!list_empty(&c->ec_stripe_new_list));

	free_heap(&c->ec_stripes_heap);
	genradix_free(&c->stripes);
	bioset_exit(&c->ec_bioset);
}

void bch2_fs_ec_init_early(struct bch_fs *c)
{
	spin_lock_init(&c->ec_stripes_new_lock);
	mutex_init(&c->ec_stripes_heap_lock);

	INIT_LIST_HEAD(&c->ec_stripe_head_list);
	mutex_init(&c->ec_stripe_head_lock);

	INIT_LIST_HEAD(&c->ec_stripe_new_list);
	mutex_init(&c->ec_stripe_new_lock);
	init_waitqueue_head(&c->ec_stripe_new_wait);

	INIT_WORK(&c->ec_stripe_create_work, ec_stripe_create_work);
	INIT_WORK(&c->ec_stripe_delete_work, ec_stripe_delete_work);
}

int bch2_fs_ec_init(struct bch_fs *c)
{
	return bioset_init(&c->ec_bioset, 1, offsetof(struct ec_bio, bio),
			   BIOSET_NEED_BVECS);
}
