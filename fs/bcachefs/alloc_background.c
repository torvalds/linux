// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "backpointers.h"
#include "bkey_buf.h"
#include "btree_cache.h"
#include "btree_io.h"
#include "btree_key_cache.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_gc.h"
#include "btree_write_buffer.h"
#include "buckets.h"
#include "buckets_waiting_for_journal.h"
#include "clock.h"
#include "debug.h"
#include "disk_accounting.h"
#include "ec.h"
#include "error.h"
#include "lru.h"
#include "recovery.h"
#include "trace.h"
#include "varint.h"

#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>
#include <linux/sort.h>
#include <linux/jiffies.h>

static void bch2_discard_one_bucket_fast(struct bch_dev *, u64);

/* Persistent alloc info: */

static const unsigned BCH_ALLOC_V1_FIELD_BYTES[] = {
#define x(name, bits) [BCH_ALLOC_FIELD_V1_##name] = bits / 8,
	BCH_ALLOC_FIELDS_V1()
#undef x
};

struct bkey_alloc_unpacked {
	u64		journal_seq;
	u8		gen;
	u8		oldest_gen;
	u8		data_type;
	bool		need_discard:1;
	bool		need_inc_gen:1;
#define x(_name, _bits)	u##_bits _name;
	BCH_ALLOC_FIELDS_V2()
#undef  x
};

static inline u64 alloc_field_v1_get(const struct bch_alloc *a,
				     const void **p, unsigned field)
{
	unsigned bytes = BCH_ALLOC_V1_FIELD_BYTES[field];
	u64 v;

	if (!(a->fields & (1 << field)))
		return 0;

	switch (bytes) {
	case 1:
		v = *((const u8 *) *p);
		break;
	case 2:
		v = le16_to_cpup(*p);
		break;
	case 4:
		v = le32_to_cpup(*p);
		break;
	case 8:
		v = le64_to_cpup(*p);
		break;
	default:
		BUG();
	}

	*p += bytes;
	return v;
}

static void bch2_alloc_unpack_v1(struct bkey_alloc_unpacked *out,
				 struct bkey_s_c k)
{
	const struct bch_alloc *in = bkey_s_c_to_alloc(k).v;
	const void *d = in->data;
	unsigned idx = 0;

	out->gen = in->gen;

#define x(_name, _bits) out->_name = alloc_field_v1_get(in, &d, idx++);
	BCH_ALLOC_FIELDS_V1()
#undef  x
}

static int bch2_alloc_unpack_v2(struct bkey_alloc_unpacked *out,
				struct bkey_s_c k)
{
	struct bkey_s_c_alloc_v2 a = bkey_s_c_to_alloc_v2(k);
	const u8 *in = a.v->data;
	const u8 *end = bkey_val_end(a);
	unsigned fieldnr = 0;
	int ret;
	u64 v;

	out->gen	= a.v->gen;
	out->oldest_gen	= a.v->oldest_gen;
	out->data_type	= a.v->data_type;

#define x(_name, _bits)							\
	if (fieldnr < a.v->nr_fields) {					\
		ret = bch2_varint_decode_fast(in, end, &v);		\
		if (ret < 0)						\
			return ret;					\
		in += ret;						\
	} else {							\
		v = 0;							\
	}								\
	out->_name = v;							\
	if (v != out->_name)						\
		return -1;						\
	fieldnr++;

	BCH_ALLOC_FIELDS_V2()
#undef  x
	return 0;
}

static int bch2_alloc_unpack_v3(struct bkey_alloc_unpacked *out,
				struct bkey_s_c k)
{
	struct bkey_s_c_alloc_v3 a = bkey_s_c_to_alloc_v3(k);
	const u8 *in = a.v->data;
	const u8 *end = bkey_val_end(a);
	unsigned fieldnr = 0;
	int ret;
	u64 v;

	out->gen	= a.v->gen;
	out->oldest_gen	= a.v->oldest_gen;
	out->data_type	= a.v->data_type;
	out->need_discard = BCH_ALLOC_V3_NEED_DISCARD(a.v);
	out->need_inc_gen = BCH_ALLOC_V3_NEED_INC_GEN(a.v);
	out->journal_seq = le64_to_cpu(a.v->journal_seq);

#define x(_name, _bits)							\
	if (fieldnr < a.v->nr_fields) {					\
		ret = bch2_varint_decode_fast(in, end, &v);		\
		if (ret < 0)						\
			return ret;					\
		in += ret;						\
	} else {							\
		v = 0;							\
	}								\
	out->_name = v;							\
	if (v != out->_name)						\
		return -1;						\
	fieldnr++;

	BCH_ALLOC_FIELDS_V2()
#undef  x
	return 0;
}

static struct bkey_alloc_unpacked bch2_alloc_unpack(struct bkey_s_c k)
{
	struct bkey_alloc_unpacked ret = { .gen	= 0 };

	switch (k.k->type) {
	case KEY_TYPE_alloc:
		bch2_alloc_unpack_v1(&ret, k);
		break;
	case KEY_TYPE_alloc_v2:
		bch2_alloc_unpack_v2(&ret, k);
		break;
	case KEY_TYPE_alloc_v3:
		bch2_alloc_unpack_v3(&ret, k);
		break;
	}

	return ret;
}

static unsigned bch_alloc_v1_val_u64s(const struct bch_alloc *a)
{
	unsigned i, bytes = offsetof(struct bch_alloc, data);

	for (i = 0; i < ARRAY_SIZE(BCH_ALLOC_V1_FIELD_BYTES); i++)
		if (a->fields & (1 << i))
			bytes += BCH_ALLOC_V1_FIELD_BYTES[i];

	return DIV_ROUND_UP(bytes, sizeof(u64));
}

int bch2_alloc_v1_validate(struct bch_fs *c, struct bkey_s_c k,
			   struct bkey_validate_context from)
{
	struct bkey_s_c_alloc a = bkey_s_c_to_alloc(k);
	int ret = 0;

	/* allow for unknown fields */
	bkey_fsck_err_on(bkey_val_u64s(a.k) < bch_alloc_v1_val_u64s(a.v),
			 c, alloc_v1_val_size_bad,
			 "incorrect value size (%zu < %u)",
			 bkey_val_u64s(a.k), bch_alloc_v1_val_u64s(a.v));
fsck_err:
	return ret;
}

int bch2_alloc_v2_validate(struct bch_fs *c, struct bkey_s_c k,
			   struct bkey_validate_context from)
{
	struct bkey_alloc_unpacked u;
	int ret = 0;

	bkey_fsck_err_on(bch2_alloc_unpack_v2(&u, k),
			 c, alloc_v2_unpack_error,
			 "unpack error");
fsck_err:
	return ret;
}

int bch2_alloc_v3_validate(struct bch_fs *c, struct bkey_s_c k,
			   struct bkey_validate_context from)
{
	struct bkey_alloc_unpacked u;
	int ret = 0;

	bkey_fsck_err_on(bch2_alloc_unpack_v3(&u, k),
			 c, alloc_v3_unpack_error,
			 "unpack error");
fsck_err:
	return ret;
}

int bch2_alloc_v4_validate(struct bch_fs *c, struct bkey_s_c k,
			   struct bkey_validate_context from)
{
	struct bch_alloc_v4 a;
	int ret = 0;

	bkey_val_copy(&a, bkey_s_c_to_alloc_v4(k));

	bkey_fsck_err_on(alloc_v4_u64s_noerror(&a) > bkey_val_u64s(k.k),
			 c, alloc_v4_val_size_bad,
			 "bad val size (%u > %zu)",
			 alloc_v4_u64s_noerror(&a), bkey_val_u64s(k.k));

	bkey_fsck_err_on(!BCH_ALLOC_V4_BACKPOINTERS_START(&a) &&
			 BCH_ALLOC_V4_NR_BACKPOINTERS(&a),
			 c, alloc_v4_backpointers_start_bad,
			 "invalid backpointers_start");

	bkey_fsck_err_on(alloc_data_type(a, a.data_type) != a.data_type,
			 c, alloc_key_data_type_bad,
			 "invalid data type (got %u should be %u)",
			 a.data_type, alloc_data_type(a, a.data_type));

	for (unsigned i = 0; i < 2; i++)
		bkey_fsck_err_on(a.io_time[i] > LRU_TIME_MAX,
				 c, alloc_key_io_time_bad,
				 "invalid io_time[%s]: %llu, max %llu",
				 i == READ ? "read" : "write",
				 a.io_time[i], LRU_TIME_MAX);

	unsigned stripe_sectors = BCH_ALLOC_V4_BACKPOINTERS_START(&a) * sizeof(u64) >
		offsetof(struct bch_alloc_v4, stripe_sectors)
		? a.stripe_sectors
		: 0;

	switch (a.data_type) {
	case BCH_DATA_free:
	case BCH_DATA_need_gc_gens:
	case BCH_DATA_need_discard:
		bkey_fsck_err_on(stripe_sectors ||
				 a.dirty_sectors ||
				 a.cached_sectors ||
				 a.stripe,
				 c, alloc_key_empty_but_have_data,
				 "empty data type free but have data %u.%u.%u %u",
				 stripe_sectors,
				 a.dirty_sectors,
				 a.cached_sectors,
				 a.stripe);
		break;
	case BCH_DATA_sb:
	case BCH_DATA_journal:
	case BCH_DATA_btree:
	case BCH_DATA_user:
	case BCH_DATA_parity:
		bkey_fsck_err_on(!a.dirty_sectors &&
				 !stripe_sectors,
				 c, alloc_key_dirty_sectors_0,
				 "data_type %s but dirty_sectors==0",
				 bch2_data_type_str(a.data_type));
		break;
	case BCH_DATA_cached:
		bkey_fsck_err_on(!a.cached_sectors ||
				 a.dirty_sectors ||
				 stripe_sectors ||
				 a.stripe,
				 c, alloc_key_cached_inconsistency,
				 "data type inconsistency");

		bkey_fsck_err_on(!a.io_time[READ] &&
				 c->curr_recovery_pass > BCH_RECOVERY_PASS_check_alloc_to_lru_refs,
				 c, alloc_key_cached_but_read_time_zero,
				 "cached bucket with read_time == 0");
		break;
	case BCH_DATA_stripe:
		break;
	}
fsck_err:
	return ret;
}

void bch2_alloc_v4_swab(struct bkey_s k)
{
	struct bch_alloc_v4 *a = bkey_s_to_alloc_v4(k).v;

	a->journal_seq_nonempty	= swab64(a->journal_seq_nonempty);
	a->journal_seq_empty	= swab64(a->journal_seq_empty);
	a->flags		= swab32(a->flags);
	a->dirty_sectors	= swab32(a->dirty_sectors);
	a->cached_sectors	= swab32(a->cached_sectors);
	a->io_time[0]		= swab64(a->io_time[0]);
	a->io_time[1]		= swab64(a->io_time[1]);
	a->stripe		= swab32(a->stripe);
	a->nr_external_backpointers = swab32(a->nr_external_backpointers);
	a->stripe_sectors	= swab32(a->stripe_sectors);
}

void bch2_alloc_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bch_alloc_v4 _a;
	const struct bch_alloc_v4 *a = bch2_alloc_to_v4(k, &_a);
	struct bch_dev *ca = c ? bch2_dev_bucket_tryget_noerror(c, k.k->p) : NULL;

	prt_newline(out);
	printbuf_indent_add(out, 2);

	prt_printf(out, "gen %u oldest_gen %u data_type ", a->gen, a->oldest_gen);
	bch2_prt_data_type(out, a->data_type);
	prt_newline(out);
	prt_printf(out, "journal_seq_nonempty %llu\n",	a->journal_seq_nonempty);
	prt_printf(out, "journal_seq_empty    %llu\n",	a->journal_seq_empty);
	prt_printf(out, "need_discard         %llu\n",	BCH_ALLOC_V4_NEED_DISCARD(a));
	prt_printf(out, "need_inc_gen         %llu\n",	BCH_ALLOC_V4_NEED_INC_GEN(a));
	prt_printf(out, "dirty_sectors        %u\n",	a->dirty_sectors);
	prt_printf(out, "stripe_sectors       %u\n",	a->stripe_sectors);
	prt_printf(out, "cached_sectors       %u\n",	a->cached_sectors);
	prt_printf(out, "stripe               %u\n",	a->stripe);
	prt_printf(out, "stripe_redundancy    %u\n",	a->stripe_redundancy);
	prt_printf(out, "io_time[READ]        %llu\n",	a->io_time[READ]);
	prt_printf(out, "io_time[WRITE]       %llu\n",	a->io_time[WRITE]);

	if (ca)
		prt_printf(out, "fragmentation     %llu\n",	alloc_lru_idx_fragmentation(*a, ca));
	prt_printf(out, "bp_start          %llu\n", BCH_ALLOC_V4_BACKPOINTERS_START(a));
	printbuf_indent_sub(out, 2);

	bch2_dev_put(ca);
}

void __bch2_alloc_to_v4(struct bkey_s_c k, struct bch_alloc_v4 *out)
{
	if (k.k->type == KEY_TYPE_alloc_v4) {
		void *src, *dst;

		*out = *bkey_s_c_to_alloc_v4(k).v;

		src = alloc_v4_backpointers(out);
		SET_BCH_ALLOC_V4_BACKPOINTERS_START(out, BCH_ALLOC_V4_U64s);
		dst = alloc_v4_backpointers(out);

		if (src < dst)
			memset(src, 0, dst - src);

		SET_BCH_ALLOC_V4_NR_BACKPOINTERS(out, 0);
	} else {
		struct bkey_alloc_unpacked u = bch2_alloc_unpack(k);

		*out = (struct bch_alloc_v4) {
			.journal_seq_nonempty	= u.journal_seq,
			.flags			= u.need_discard,
			.gen			= u.gen,
			.oldest_gen		= u.oldest_gen,
			.data_type		= u.data_type,
			.stripe_redundancy	= u.stripe_redundancy,
			.dirty_sectors		= u.dirty_sectors,
			.cached_sectors		= u.cached_sectors,
			.io_time[READ]		= u.read_time,
			.io_time[WRITE]		= u.write_time,
			.stripe			= u.stripe,
		};

		SET_BCH_ALLOC_V4_BACKPOINTERS_START(out, BCH_ALLOC_V4_U64s);
	}
}

static noinline struct bkey_i_alloc_v4 *
__bch2_alloc_to_v4_mut(struct btree_trans *trans, struct bkey_s_c k)
{
	struct bkey_i_alloc_v4 *ret;

	ret = bch2_trans_kmalloc(trans, max(bkey_bytes(k.k), sizeof(struct bkey_i_alloc_v4)));
	if (IS_ERR(ret))
		return ret;

	if (k.k->type == KEY_TYPE_alloc_v4) {
		void *src, *dst;

		bkey_reassemble(&ret->k_i, k);

		src = alloc_v4_backpointers(&ret->v);
		SET_BCH_ALLOC_V4_BACKPOINTERS_START(&ret->v, BCH_ALLOC_V4_U64s);
		dst = alloc_v4_backpointers(&ret->v);

		if (src < dst)
			memset(src, 0, dst - src);

		SET_BCH_ALLOC_V4_NR_BACKPOINTERS(&ret->v, 0);
		set_alloc_v4_u64s(ret);
	} else {
		bkey_alloc_v4_init(&ret->k_i);
		ret->k.p = k.k->p;
		bch2_alloc_to_v4(k, &ret->v);
	}
	return ret;
}

static inline struct bkey_i_alloc_v4 *bch2_alloc_to_v4_mut_inlined(struct btree_trans *trans, struct bkey_s_c k)
{
	struct bkey_s_c_alloc_v4 a;

	if (likely(k.k->type == KEY_TYPE_alloc_v4) &&
	    ((a = bkey_s_c_to_alloc_v4(k), true) &&
	     BCH_ALLOC_V4_NR_BACKPOINTERS(a.v) == 0))
		return bch2_bkey_make_mut_noupdate_typed(trans, k, alloc_v4);

	return __bch2_alloc_to_v4_mut(trans, k);
}

struct bkey_i_alloc_v4 *bch2_alloc_to_v4_mut(struct btree_trans *trans, struct bkey_s_c k)
{
	return bch2_alloc_to_v4_mut_inlined(trans, k);
}

struct bkey_i_alloc_v4 *
bch2_trans_start_alloc_update_noupdate(struct btree_trans *trans, struct btree_iter *iter,
				       struct bpos pos)
{
	struct bkey_s_c k = bch2_bkey_get_iter(trans, iter, BTREE_ID_alloc, pos,
					       BTREE_ITER_with_updates|
					       BTREE_ITER_cached|
					       BTREE_ITER_intent);
	int ret = bkey_err(k);
	if (unlikely(ret))
		return ERR_PTR(ret);

	struct bkey_i_alloc_v4 *a = bch2_alloc_to_v4_mut_inlined(trans, k);
	ret = PTR_ERR_OR_ZERO(a);
	if (unlikely(ret))
		goto err;
	return a;
err:
	bch2_trans_iter_exit(trans, iter);
	return ERR_PTR(ret);
}

__flatten
struct bkey_i_alloc_v4 *bch2_trans_start_alloc_update(struct btree_trans *trans, struct bpos pos,
						      enum btree_iter_update_trigger_flags flags)
{
	struct btree_iter iter;
	struct bkey_i_alloc_v4 *a = bch2_trans_start_alloc_update_noupdate(trans, &iter, pos);
	int ret = PTR_ERR_OR_ZERO(a);
	if (ret)
		return ERR_PTR(ret);

	ret = bch2_trans_update(trans, &iter, &a->k_i, flags);
	bch2_trans_iter_exit(trans, &iter);
	return unlikely(ret) ? ERR_PTR(ret) : a;
}

static struct bpos alloc_gens_pos(struct bpos pos, unsigned *offset)
{
	*offset = pos.offset & KEY_TYPE_BUCKET_GENS_MASK;

	pos.offset >>= KEY_TYPE_BUCKET_GENS_BITS;
	return pos;
}

static struct bpos bucket_gens_pos_to_alloc(struct bpos pos, unsigned offset)
{
	pos.offset <<= KEY_TYPE_BUCKET_GENS_BITS;
	pos.offset += offset;
	return pos;
}

static unsigned alloc_gen(struct bkey_s_c k, unsigned offset)
{
	return k.k->type == KEY_TYPE_bucket_gens
		? bkey_s_c_to_bucket_gens(k).v->gens[offset]
		: 0;
}

int bch2_bucket_gens_validate(struct bch_fs *c, struct bkey_s_c k,
			      struct bkey_validate_context from)
{
	int ret = 0;

	bkey_fsck_err_on(bkey_val_bytes(k.k) != sizeof(struct bch_bucket_gens),
			 c, bucket_gens_val_size_bad,
			 "bad val size (%zu != %zu)",
			 bkey_val_bytes(k.k), sizeof(struct bch_bucket_gens));
fsck_err:
	return ret;
}

void bch2_bucket_gens_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_bucket_gens g = bkey_s_c_to_bucket_gens(k);
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(g.v->gens); i++) {
		if (i)
			prt_char(out, ' ');
		prt_printf(out, "%u", g.v->gens[i]);
	}
}

int bch2_bucket_gens_init(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct bkey_i_bucket_gens g;
	bool have_bucket_gens_key = false;
	int ret;

	ret = for_each_btree_key(trans, iter, BTREE_ID_alloc, POS_MIN,
				 BTREE_ITER_prefetch, k, ({
		/*
		 * Not a fsck error because this is checked/repaired by
		 * bch2_check_alloc_key() which runs later:
		 */
		if (!bch2_dev_bucket_exists(c, k.k->p))
			continue;

		struct bch_alloc_v4 a;
		u8 gen = bch2_alloc_to_v4(k, &a)->gen;
		unsigned offset;
		struct bpos pos = alloc_gens_pos(iter.pos, &offset);
		int ret2 = 0;

		if (have_bucket_gens_key && !bkey_eq(g.k.p, pos)) {
			ret2 =  bch2_btree_insert_trans(trans, BTREE_ID_bucket_gens, &g.k_i, 0) ?:
				bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc);
			if (ret2)
				goto iter_err;
			have_bucket_gens_key = false;
		}

		if (!have_bucket_gens_key) {
			bkey_bucket_gens_init(&g.k_i);
			g.k.p = pos;
			have_bucket_gens_key = true;
		}

		g.v.gens[offset] = gen;
iter_err:
		ret2;
	}));

	if (have_bucket_gens_key && !ret)
		ret = commit_do(trans, NULL, NULL,
				BCH_TRANS_COMMIT_no_enospc,
			bch2_btree_insert_trans(trans, BTREE_ID_bucket_gens, &g.k_i, 0));

	bch2_trans_put(trans);

	bch_err_fn(c, ret);
	return ret;
}

int bch2_alloc_read(struct bch_fs *c)
{
	down_read(&c->state_lock);

	struct btree_trans *trans = bch2_trans_get(c);
	struct bch_dev *ca = NULL;
	int ret;

	if (c->sb.version_upgrade_complete >= bcachefs_metadata_version_bucket_gens) {
		ret = for_each_btree_key(trans, iter, BTREE_ID_bucket_gens, POS_MIN,
					 BTREE_ITER_prefetch, k, ({
			u64 start = bucket_gens_pos_to_alloc(k.k->p, 0).offset;
			u64 end = bucket_gens_pos_to_alloc(bpos_nosnap_successor(k.k->p), 0).offset;

			if (k.k->type != KEY_TYPE_bucket_gens)
				continue;

			ca = bch2_dev_iterate(c, ca, k.k->p.inode);
			/*
			 * Not a fsck error because this is checked/repaired by
			 * bch2_check_alloc_key() which runs later:
			 */
			if (!ca) {
				bch2_btree_iter_set_pos(&iter, POS(k.k->p.inode + 1, 0));
				continue;
			}

			const struct bch_bucket_gens *g = bkey_s_c_to_bucket_gens(k).v;

			for (u64 b = max_t(u64, ca->mi.first_bucket, start);
			     b < min_t(u64, ca->mi.nbuckets, end);
			     b++)
				*bucket_gen(ca, b) = g->gens[b & KEY_TYPE_BUCKET_GENS_MASK];
			0;
		}));
	} else {
		ret = for_each_btree_key(trans, iter, BTREE_ID_alloc, POS_MIN,
					 BTREE_ITER_prefetch, k, ({
			ca = bch2_dev_iterate(c, ca, k.k->p.inode);
			/*
			 * Not a fsck error because this is checked/repaired by
			 * bch2_check_alloc_key() which runs later:
			 */
			if (!ca) {
				bch2_btree_iter_set_pos(&iter, POS(k.k->p.inode + 1, 0));
				continue;
			}

			if (k.k->p.offset < ca->mi.first_bucket) {
				bch2_btree_iter_set_pos(&iter, POS(k.k->p.inode, ca->mi.first_bucket));
				continue;
			}

			if (k.k->p.offset >= ca->mi.nbuckets) {
				bch2_btree_iter_set_pos(&iter, POS(k.k->p.inode + 1, 0));
				continue;
			}

			struct bch_alloc_v4 a;
			*bucket_gen(ca, k.k->p.offset) = bch2_alloc_to_v4(k, &a)->gen;
			0;
		}));
	}

	bch2_dev_put(ca);
	bch2_trans_put(trans);

	up_read(&c->state_lock);
	bch_err_fn(c, ret);
	return ret;
}

/* Free space/discard btree: */

static int __need_discard_or_freespace_err(struct btree_trans *trans,
					   struct bkey_s_c alloc_k,
					   bool set, bool discard, bool repair)
{
	struct bch_fs *c = trans->c;
	enum bch_fsck_flags flags = FSCK_CAN_IGNORE|(repair ? FSCK_CAN_FIX : 0);
	enum bch_sb_error_id err_id = discard
		? BCH_FSCK_ERR_need_discard_key_wrong
		: BCH_FSCK_ERR_freespace_key_wrong;
	enum btree_id btree = discard ? BTREE_ID_need_discard : BTREE_ID_freespace;
	struct printbuf buf = PRINTBUF;

	bch2_bkey_val_to_text(&buf, c, alloc_k);

	int ret = __bch2_fsck_err(NULL, trans, flags, err_id,
				  "bucket incorrectly %sset in %s btree\n%s",
				  set ? "" : "un",
				  bch2_btree_id_str(btree),
				  buf.buf);
	if (ret == -BCH_ERR_fsck_ignore ||
	    ret == -BCH_ERR_fsck_errors_not_fixed)
		ret = 0;

	printbuf_exit(&buf);
	return ret;
}

#define need_discard_or_freespace_err(...)		\
	fsck_err_wrap(__need_discard_or_freespace_err(__VA_ARGS__))

#define need_discard_or_freespace_err_on(cond, ...)		\
	(unlikely(cond) ?  need_discard_or_freespace_err(__VA_ARGS__) : false)

static int bch2_bucket_do_index(struct btree_trans *trans,
				struct bch_dev *ca,
				struct bkey_s_c alloc_k,
				const struct bch_alloc_v4 *a,
				bool set)
{
	enum btree_id btree;
	struct bpos pos;

	if (a->data_type != BCH_DATA_free &&
	    a->data_type != BCH_DATA_need_discard)
		return 0;

	switch (a->data_type) {
	case BCH_DATA_free:
		btree = BTREE_ID_freespace;
		pos = alloc_freespace_pos(alloc_k.k->p, *a);
		break;
	case BCH_DATA_need_discard:
		btree = BTREE_ID_need_discard;
		pos = alloc_k.k->p;
		break;
	default:
		return 0;
	}

	struct btree_iter iter;
	struct bkey_s_c old = bch2_bkey_get_iter(trans, &iter, btree, pos, BTREE_ITER_intent);
	int ret = bkey_err(old);
	if (ret)
		return ret;

	need_discard_or_freespace_err_on(ca->mi.freespace_initialized &&
					 !old.k->type != set,
					 trans, alloc_k, set,
					 btree == BTREE_ID_need_discard, false);

	ret = bch2_btree_bit_mod_iter(trans, &iter, set);
fsck_err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static noinline int bch2_bucket_gen_update(struct btree_trans *trans,
					   struct bpos bucket, u8 gen)
{
	struct btree_iter iter;
	unsigned offset;
	struct bpos pos = alloc_gens_pos(bucket, &offset);
	struct bkey_i_bucket_gens *g;
	struct bkey_s_c k;
	int ret;

	g = bch2_trans_kmalloc(trans, sizeof(*g));
	ret = PTR_ERR_OR_ZERO(g);
	if (ret)
		return ret;

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_bucket_gens, pos,
			       BTREE_ITER_intent|
			       BTREE_ITER_with_updates);
	ret = bkey_err(k);
	if (ret)
		return ret;

	if (k.k->type != KEY_TYPE_bucket_gens) {
		bkey_bucket_gens_init(&g->k_i);
		g->k.p = iter.pos;
	} else {
		bkey_reassemble(&g->k_i, k);
	}

	g->v.gens[offset] = gen;

	ret = bch2_trans_update(trans, &iter, &g->k_i, 0);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static inline int bch2_dev_data_type_accounting_mod(struct btree_trans *trans, struct bch_dev *ca,
						    enum bch_data_type data_type,
						    s64 delta_buckets,
						    s64 delta_sectors,
						    s64 delta_fragmented, unsigned flags)
{
	s64 d[3] = { delta_buckets, delta_sectors, delta_fragmented };

	return bch2_disk_accounting_mod2(trans, flags & BTREE_TRIGGER_gc,
					 d, dev_data_type,
					 .dev		= ca->dev_idx,
					 .data_type	= data_type);
}

int bch2_alloc_key_to_dev_counters(struct btree_trans *trans, struct bch_dev *ca,
				   const struct bch_alloc_v4 *old,
				   const struct bch_alloc_v4 *new,
				   unsigned flags)
{
	s64 old_sectors = bch2_bucket_sectors(*old);
	s64 new_sectors = bch2_bucket_sectors(*new);
	if (old->data_type != new->data_type) {
		int ret = bch2_dev_data_type_accounting_mod(trans, ca, new->data_type,
				 1,  new_sectors,  bch2_bucket_sectors_fragmented(ca, *new), flags) ?:
			  bch2_dev_data_type_accounting_mod(trans, ca, old->data_type,
				-1, -old_sectors, -bch2_bucket_sectors_fragmented(ca, *old), flags);
		if (ret)
			return ret;
	} else if (old_sectors != new_sectors) {
		int ret = bch2_dev_data_type_accounting_mod(trans, ca, new->data_type,
					 0,
					 new_sectors - old_sectors,
					 bch2_bucket_sectors_fragmented(ca, *new) -
					 bch2_bucket_sectors_fragmented(ca, *old), flags);
		if (ret)
			return ret;
	}

	s64 old_unstriped = bch2_bucket_sectors_unstriped(*old);
	s64 new_unstriped = bch2_bucket_sectors_unstriped(*new);
	if (old_unstriped != new_unstriped) {
		int ret = bch2_dev_data_type_accounting_mod(trans, ca, BCH_DATA_unstriped,
					 !!new_unstriped - !!old_unstriped,
					 new_unstriped - old_unstriped,
					 0,
					 flags);
		if (ret)
			return ret;
	}

	return 0;
}

int bch2_trigger_alloc(struct btree_trans *trans,
		       enum btree_id btree, unsigned level,
		       struct bkey_s_c old, struct bkey_s new,
		       enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	struct bch_dev *ca = bch2_dev_bucket_tryget(c, new.k->p);
	if (!ca)
		return -BCH_ERR_trigger_alloc;

	struct bch_alloc_v4 old_a_convert;
	const struct bch_alloc_v4 *old_a = bch2_alloc_to_v4(old, &old_a_convert);

	struct bch_alloc_v4 *new_a;
	if (likely(new.k->type == KEY_TYPE_alloc_v4)) {
		new_a = bkey_s_to_alloc_v4(new).v;
	} else {
		BUG_ON(!(flags & (BTREE_TRIGGER_gc|BTREE_TRIGGER_check_repair)));

		struct bkey_i_alloc_v4 *new_ka = bch2_alloc_to_v4_mut_inlined(trans, new.s_c);
		ret = PTR_ERR_OR_ZERO(new_ka);
		if (unlikely(ret))
			goto err;
		new_a = &new_ka->v;
	}

	if (flags & BTREE_TRIGGER_transactional) {
		alloc_data_type_set(new_a, new_a->data_type);

		int is_empty_delta = (int) data_type_is_empty(new_a->data_type) -
				     (int) data_type_is_empty(old_a->data_type);

		if (is_empty_delta < 0) {
			new_a->io_time[READ] = bch2_current_io_time(c, READ);
			new_a->io_time[WRITE]= bch2_current_io_time(c, WRITE);
			SET_BCH_ALLOC_V4_NEED_INC_GEN(new_a, true);
			SET_BCH_ALLOC_V4_NEED_DISCARD(new_a, true);
		}

		if (data_type_is_empty(new_a->data_type) &&
		    BCH_ALLOC_V4_NEED_INC_GEN(new_a) &&
		    !bch2_bucket_is_open_safe(c, new.k->p.inode, new.k->p.offset)) {
			if (new_a->oldest_gen == new_a->gen &&
			    !bch2_bucket_sectors_total(*new_a))
				new_a->oldest_gen++;
			new_a->gen++;
			SET_BCH_ALLOC_V4_NEED_INC_GEN(new_a, false);
			alloc_data_type_set(new_a, new_a->data_type);
		}

		if (old_a->data_type != new_a->data_type ||
		    (new_a->data_type == BCH_DATA_free &&
		     alloc_freespace_genbits(*old_a) != alloc_freespace_genbits(*new_a))) {
			ret =   bch2_bucket_do_index(trans, ca, old, old_a, false) ?:
				bch2_bucket_do_index(trans, ca, new.s_c, new_a, true);
			if (ret)
				goto err;
		}

		if (new_a->data_type == BCH_DATA_cached &&
		    !new_a->io_time[READ])
			new_a->io_time[READ] = bch2_current_io_time(c, READ);

		ret = bch2_lru_change(trans, new.k->p.inode,
				      bucket_to_u64(new.k->p),
				      alloc_lru_idx_read(*old_a),
				      alloc_lru_idx_read(*new_a));
		if (ret)
			goto err;

		ret = bch2_lru_change(trans,
				      BCH_LRU_BUCKET_FRAGMENTATION,
				      bucket_to_u64(new.k->p),
				      alloc_lru_idx_fragmentation(*old_a, ca),
				      alloc_lru_idx_fragmentation(*new_a, ca));
		if (ret)
			goto err;

		if (old_a->gen != new_a->gen) {
			ret = bch2_bucket_gen_update(trans, new.k->p, new_a->gen);
			if (ret)
				goto err;
		}

		if ((flags & BTREE_TRIGGER_bucket_invalidate) &&
		    old_a->cached_sectors) {
			ret = bch2_mod_dev_cached_sectors(trans, ca->dev_idx,
					 -((s64) old_a->cached_sectors),
					 flags & BTREE_TRIGGER_gc);
			if (ret)
				goto err;
		}

		ret = bch2_alloc_key_to_dev_counters(trans, ca, old_a, new_a, flags);
		if (ret)
			goto err;
	}

	if ((flags & BTREE_TRIGGER_atomic) && (flags & BTREE_TRIGGER_insert)) {
		u64 transaction_seq = trans->journal_res.seq;
		BUG_ON(!transaction_seq);

		if (log_fsck_err_on(transaction_seq && new_a->journal_seq_nonempty > transaction_seq,
				    trans, alloc_key_journal_seq_in_future,
				    "bucket journal seq in future (currently at %llu)\n%s",
				    journal_cur_seq(&c->journal),
				    (bch2_bkey_val_to_text(&buf, c, new.s_c), buf.buf)))
			new_a->journal_seq_nonempty = transaction_seq;

		int is_empty_delta = (int) data_type_is_empty(new_a->data_type) -
				     (int) data_type_is_empty(old_a->data_type);

		/*
		 * Record journal sequence number of empty -> nonempty transition:
		 * Note that there may be multiple empty -> nonempty
		 * transitions, data in a bucket may be overwritten while we're
		 * still writing to it - so be careful to only record the first:
		 * */
		if (is_empty_delta < 0 &&
		    new_a->journal_seq_empty <= c->journal.flushed_seq_ondisk) {
			new_a->journal_seq_nonempty	= transaction_seq;
			new_a->journal_seq_empty	= 0;
		}

		/*
		 * Bucket becomes empty: mark it as waiting for a journal flush,
		 * unless updates since empty -> nonempty transition were never
		 * flushed - we may need to ask the journal not to flush
		 * intermediate sequence numbers:
		 */
		if (is_empty_delta > 0) {
			if (new_a->journal_seq_nonempty == transaction_seq ||
			    bch2_journal_noflush_seq(&c->journal,
						     new_a->journal_seq_nonempty,
						     transaction_seq)) {
				new_a->journal_seq_nonempty = new_a->journal_seq_empty = 0;
			} else {
				new_a->journal_seq_empty = transaction_seq;

				ret = bch2_set_bucket_needs_journal_commit(&c->buckets_waiting_for_journal,
									   c->journal.flushed_seq_ondisk,
									   new.k->p.inode, new.k->p.offset,
									   transaction_seq);
				if (bch2_fs_fatal_err_on(ret, c,
						"setting bucket_needs_journal_commit: %s",
						bch2_err_str(ret)))
					goto err;
			}
		}

		if (new_a->gen != old_a->gen) {
			rcu_read_lock();
			u8 *gen = bucket_gen(ca, new.k->p.offset);
			if (unlikely(!gen)) {
				rcu_read_unlock();
				goto invalid_bucket;
			}
			*gen = new_a->gen;
			rcu_read_unlock();
		}

#define eval_state(_a, expr)		({ const struct bch_alloc_v4 *a = _a; expr; })
#define statechange(expr)		!eval_state(old_a, expr) && eval_state(new_a, expr)
#define bucket_flushed(a)		(a->journal_seq_empty <= c->journal.flushed_seq_ondisk)

		if (statechange(a->data_type == BCH_DATA_free) &&
		    bucket_flushed(new_a))
			closure_wake_up(&c->freelist_wait);

		if (statechange(a->data_type == BCH_DATA_need_discard) &&
		    !bch2_bucket_is_open_safe(c, new.k->p.inode, new.k->p.offset) &&
		    bucket_flushed(new_a))
			bch2_discard_one_bucket_fast(ca, new.k->p.offset);

		if (statechange(a->data_type == BCH_DATA_cached) &&
		    !bch2_bucket_is_open(c, new.k->p.inode, new.k->p.offset) &&
		    should_invalidate_buckets(ca, bch2_dev_usage_read(ca)))
			bch2_dev_do_invalidates(ca);

		if (statechange(a->data_type == BCH_DATA_need_gc_gens))
			bch2_gc_gens_async(c);
	}

	if ((flags & BTREE_TRIGGER_gc) && (flags & BTREE_TRIGGER_insert)) {
		rcu_read_lock();
		struct bucket *g = gc_bucket(ca, new.k->p.offset);
		if (unlikely(!g)) {
			rcu_read_unlock();
			goto invalid_bucket;
		}
		g->gen_valid	= 1;
		g->gen		= new_a->gen;
		rcu_read_unlock();
	}
err:
fsck_err:
	printbuf_exit(&buf);
	bch2_dev_put(ca);
	return ret;
invalid_bucket:
	bch2_fs_inconsistent(c, "reference to invalid bucket\n%s",
			     (bch2_bkey_val_to_text(&buf, c, new.s_c), buf.buf));
	ret = -BCH_ERR_trigger_alloc;
	goto err;
}

/*
 * This synthesizes deleted extents for holes, similar to BTREE_ITER_slots for
 * extents style btrees, but works on non-extents btrees:
 */
static struct bkey_s_c bch2_get_key_or_hole(struct btree_iter *iter, struct bpos end, struct bkey *hole)
{
	struct bkey_s_c k = bch2_btree_iter_peek_slot(iter);

	if (bkey_err(k))
		return k;

	if (k.k->type) {
		return k;
	} else {
		struct btree_iter iter2;
		struct bpos next;

		bch2_trans_copy_iter(&iter2, iter);

		struct btree_path *path = btree_iter_path(iter->trans, iter);
		if (!bpos_eq(path->l[0].b->key.k.p, SPOS_MAX))
			end = bkey_min(end, bpos_nosnap_successor(path->l[0].b->key.k.p));

		end = bkey_min(end, POS(iter->pos.inode, iter->pos.offset + U32_MAX - 1));

		/*
		 * btree node min/max is a closed interval, upto takes a half
		 * open interval:
		 */
		k = bch2_btree_iter_peek_max(&iter2, end);
		next = iter2.pos;
		bch2_trans_iter_exit(iter->trans, &iter2);

		BUG_ON(next.offset >= iter->pos.offset + U32_MAX);

		if (bkey_err(k))
			return k;

		bkey_init(hole);
		hole->p = iter->pos;

		bch2_key_resize(hole, next.offset - iter->pos.offset);
		return (struct bkey_s_c) { hole, NULL };
	}
}

static bool next_bucket(struct bch_fs *c, struct bch_dev **ca, struct bpos *bucket)
{
	if (*ca) {
		if (bucket->offset < (*ca)->mi.first_bucket)
			bucket->offset = (*ca)->mi.first_bucket;

		if (bucket->offset < (*ca)->mi.nbuckets)
			return true;

		bch2_dev_put(*ca);
		*ca = NULL;
		bucket->inode++;
		bucket->offset = 0;
	}

	rcu_read_lock();
	*ca = __bch2_next_dev_idx(c, bucket->inode, NULL);
	if (*ca) {
		*bucket = POS((*ca)->dev_idx, (*ca)->mi.first_bucket);
		bch2_dev_get(*ca);
	}
	rcu_read_unlock();

	return *ca != NULL;
}

static struct bkey_s_c bch2_get_key_or_real_bucket_hole(struct btree_iter *iter,
					struct bch_dev **ca, struct bkey *hole)
{
	struct bch_fs *c = iter->trans->c;
	struct bkey_s_c k;
again:
	k = bch2_get_key_or_hole(iter, POS_MAX, hole);
	if (bkey_err(k))
		return k;

	*ca = bch2_dev_iterate_noerror(c, *ca, k.k->p.inode);

	if (!k.k->type) {
		struct bpos hole_start = bkey_start_pos(k.k);

		if (!*ca || !bucket_valid(*ca, hole_start.offset)) {
			if (!next_bucket(c, ca, &hole_start))
				return bkey_s_c_null;

			bch2_btree_iter_set_pos(iter, hole_start);
			goto again;
		}

		if (k.k->p.offset > (*ca)->mi.nbuckets)
			bch2_key_resize(hole, (*ca)->mi.nbuckets - hole_start.offset);
	}

	return k;
}

static noinline_for_stack
int bch2_check_alloc_key(struct btree_trans *trans,
			 struct bkey_s_c alloc_k,
			 struct btree_iter *alloc_iter,
			 struct btree_iter *discard_iter,
			 struct btree_iter *freespace_iter,
			 struct btree_iter *bucket_gens_iter)
{
	struct bch_fs *c = trans->c;
	struct bch_alloc_v4 a_convert;
	const struct bch_alloc_v4 *a;
	unsigned gens_offset;
	struct bkey_s_c k;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	struct bch_dev *ca = bch2_dev_bucket_tryget_noerror(c, alloc_k.k->p);
	if (fsck_err_on(!ca,
			trans, alloc_key_to_missing_dev_bucket,
			"alloc key for invalid device:bucket %llu:%llu",
			alloc_k.k->p.inode, alloc_k.k->p.offset))
		ret = bch2_btree_delete_at(trans, alloc_iter, 0);
	if (!ca)
		return ret;

	if (!ca->mi.freespace_initialized)
		goto out;

	a = bch2_alloc_to_v4(alloc_k, &a_convert);

	bch2_btree_iter_set_pos(discard_iter, alloc_k.k->p);
	k = bch2_btree_iter_peek_slot(discard_iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	bool is_discarded = a->data_type == BCH_DATA_need_discard;
	if (need_discard_or_freespace_err_on(!!k.k->type != is_discarded,
					     trans, alloc_k, !is_discarded, true, true)) {
		ret = bch2_btree_bit_mod_iter(trans, discard_iter, is_discarded);
		if (ret)
			goto err;
	}

	bch2_btree_iter_set_pos(freespace_iter, alloc_freespace_pos(alloc_k.k->p, *a));
	k = bch2_btree_iter_peek_slot(freespace_iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	bool is_free = a->data_type == BCH_DATA_free;
	if (need_discard_or_freespace_err_on(!!k.k->type != is_free,
					     trans, alloc_k, !is_free, false, true)) {
		ret = bch2_btree_bit_mod_iter(trans, freespace_iter, is_free);
		if (ret)
			goto err;
	}

	bch2_btree_iter_set_pos(bucket_gens_iter, alloc_gens_pos(alloc_k.k->p, &gens_offset));
	k = bch2_btree_iter_peek_slot(bucket_gens_iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (fsck_err_on(a->gen != alloc_gen(k, gens_offset),
			trans, bucket_gens_key_wrong,
			"incorrect gen in bucket_gens btree (got %u should be %u)\n%s",
			alloc_gen(k, gens_offset), a->gen,
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, alloc_k), buf.buf))) {
		struct bkey_i_bucket_gens *g =
			bch2_trans_kmalloc(trans, sizeof(*g));

		ret = PTR_ERR_OR_ZERO(g);
		if (ret)
			goto err;

		if (k.k->type == KEY_TYPE_bucket_gens) {
			bkey_reassemble(&g->k_i, k);
		} else {
			bkey_bucket_gens_init(&g->k_i);
			g->k.p = alloc_gens_pos(alloc_k.k->p, &gens_offset);
		}

		g->v.gens[gens_offset] = a->gen;

		ret = bch2_trans_update(trans, bucket_gens_iter, &g->k_i, 0);
		if (ret)
			goto err;
	}
out:
err:
fsck_err:
	bch2_dev_put(ca);
	printbuf_exit(&buf);
	return ret;
}

static noinline_for_stack
int bch2_check_alloc_hole_freespace(struct btree_trans *trans,
				    struct bch_dev *ca,
				    struct bpos start,
				    struct bpos *end,
				    struct btree_iter *freespace_iter)
{
	struct bkey_s_c k;
	struct printbuf buf = PRINTBUF;
	int ret;

	if (!ca->mi.freespace_initialized)
		return 0;

	bch2_btree_iter_set_pos(freespace_iter, start);

	k = bch2_btree_iter_peek_slot(freespace_iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	*end = bkey_min(k.k->p, *end);

	if (fsck_err_on(k.k->type != KEY_TYPE_set,
			trans, freespace_hole_missing,
			"hole in alloc btree missing in freespace btree\n"
			"device %llu buckets %llu-%llu",
			freespace_iter->pos.inode,
			freespace_iter->pos.offset,
			end->offset)) {
		struct bkey_i *update =
			bch2_trans_kmalloc(trans, sizeof(*update));

		ret = PTR_ERR_OR_ZERO(update);
		if (ret)
			goto err;

		bkey_init(&update->k);
		update->k.type	= KEY_TYPE_set;
		update->k.p	= freespace_iter->pos;
		bch2_key_resize(&update->k,
				min_t(u64, U32_MAX, end->offset -
				      freespace_iter->pos.offset));

		ret = bch2_trans_update(trans, freespace_iter, update, 0);
		if (ret)
			goto err;
	}
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

static noinline_for_stack
int bch2_check_alloc_hole_bucket_gens(struct btree_trans *trans,
				      struct bpos start,
				      struct bpos *end,
				      struct btree_iter *bucket_gens_iter)
{
	struct bkey_s_c k;
	struct printbuf buf = PRINTBUF;
	unsigned i, gens_offset, gens_end_offset;
	int ret;

	bch2_btree_iter_set_pos(bucket_gens_iter, alloc_gens_pos(start, &gens_offset));

	k = bch2_btree_iter_peek_slot(bucket_gens_iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (bkey_cmp(alloc_gens_pos(start, &gens_offset),
		     alloc_gens_pos(*end,  &gens_end_offset)))
		gens_end_offset = KEY_TYPE_BUCKET_GENS_NR;

	if (k.k->type == KEY_TYPE_bucket_gens) {
		struct bkey_i_bucket_gens g;
		bool need_update = false;

		bkey_reassemble(&g.k_i, k);

		for (i = gens_offset; i < gens_end_offset; i++) {
			if (fsck_err_on(g.v.gens[i], trans,
					bucket_gens_hole_wrong,
					"hole in alloc btree at %llu:%llu with nonzero gen in bucket_gens btree (%u)",
					bucket_gens_pos_to_alloc(k.k->p, i).inode,
					bucket_gens_pos_to_alloc(k.k->p, i).offset,
					g.v.gens[i])) {
				g.v.gens[i] = 0;
				need_update = true;
			}
		}

		if (need_update) {
			struct bkey_i *u = bch2_trans_kmalloc(trans, sizeof(g));

			ret = PTR_ERR_OR_ZERO(u);
			if (ret)
				goto err;

			memcpy(u, &g, sizeof(g));

			ret = bch2_trans_update(trans, bucket_gens_iter, u, 0);
			if (ret)
				goto err;
		}
	}

	*end = bkey_min(*end, bucket_gens_pos_to_alloc(bpos_nosnap_successor(k.k->p), 0));
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

struct check_discard_freespace_key_async {
	struct work_struct	work;
	struct bch_fs		*c;
	struct bbpos		pos;
};

static int bch2_recheck_discard_freespace_key(struct btree_trans *trans, struct bbpos pos)
{
	struct btree_iter iter;
	struct bkey_s_c k = bch2_bkey_get_iter(trans, &iter, pos.btree, pos.pos, 0);
	int ret = bkey_err(k);
	if (ret)
		return ret;

	u8 gen;
	ret = k.k->type != KEY_TYPE_set
		? bch2_check_discard_freespace_key(trans, &iter, &gen, false)
		: 0;
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static void check_discard_freespace_key_work(struct work_struct *work)
{
	struct check_discard_freespace_key_async *w =
		container_of(work, struct check_discard_freespace_key_async, work);

	bch2_trans_do(w->c, bch2_recheck_discard_freespace_key(trans, w->pos));
	bch2_write_ref_put(w->c, BCH_WRITE_REF_check_discard_freespace_key);
	kfree(w);
}

int bch2_check_discard_freespace_key(struct btree_trans *trans, struct btree_iter *iter, u8 *gen,
				     bool async_repair)
{
	struct bch_fs *c = trans->c;
	enum bch_data_type state = iter->btree_id == BTREE_ID_need_discard
		? BCH_DATA_need_discard
		: BCH_DATA_free;
	struct printbuf buf = PRINTBUF;

	struct bpos bucket = iter->pos;
	bucket.offset &= ~(~0ULL << 56);
	u64 genbits = iter->pos.offset & (~0ULL << 56);

	struct btree_iter alloc_iter;
	struct bkey_s_c alloc_k = bch2_bkey_get_iter(trans, &alloc_iter,
						     BTREE_ID_alloc, bucket,
						     async_repair ? BTREE_ITER_cached : 0);
	int ret = bkey_err(alloc_k);
	if (ret)
		return ret;

	if (!bch2_dev_bucket_exists(c, bucket)) {
		if (fsck_err(trans, need_discard_freespace_key_to_invalid_dev_bucket,
			     "entry in %s btree for nonexistant dev:bucket %llu:%llu",
			     bch2_btree_id_str(iter->btree_id), bucket.inode, bucket.offset))
			goto delete;
		ret = 1;
		goto out;
	}

	struct bch_alloc_v4 a_convert;
	const struct bch_alloc_v4 *a = bch2_alloc_to_v4(alloc_k, &a_convert);

	if (a->data_type != state ||
	    (state == BCH_DATA_free &&
	     genbits != alloc_freespace_genbits(*a))) {
		if (fsck_err(trans, need_discard_freespace_key_bad,
			     "%s\nincorrectly set at %s:%llu:%llu:0 (free %u, genbits %llu should be %llu)",
			     (bch2_bkey_val_to_text(&buf, c, alloc_k), buf.buf),
			     bch2_btree_id_str(iter->btree_id),
			     iter->pos.inode,
			     iter->pos.offset,
			     a->data_type == state,
			     genbits >> 56, alloc_freespace_genbits(*a) >> 56))
			goto delete;
		ret = 1;
		goto out;
	}

	*gen = a->gen;
out:
fsck_err:
	bch2_set_btree_iter_dontneed(&alloc_iter);
	bch2_trans_iter_exit(trans, &alloc_iter);
	printbuf_exit(&buf);
	return ret;
delete:
	if (!async_repair) {
		ret =   bch2_btree_bit_mod_iter(trans, iter, false) ?:
			bch2_trans_commit(trans, NULL, NULL,
				BCH_TRANS_COMMIT_no_enospc) ?:
			-BCH_ERR_transaction_restart_commit;
		goto out;
	} else {
		/*
		 * We can't repair here when called from the allocator path: the
		 * commit will recurse back into the allocator
		 */
		struct check_discard_freespace_key_async *w =
			kzalloc(sizeof(*w), GFP_KERNEL);
		if (!w)
			goto out;

		if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_check_discard_freespace_key)) {
			kfree(w);
			goto out;
		}

		INIT_WORK(&w->work, check_discard_freespace_key_work);
		w->c = c;
		w->pos = BBPOS(iter->btree_id, iter->pos);
		queue_work(c->write_ref_wq, &w->work);
		goto out;
	}
}

static int bch2_check_discard_freespace_key_fsck(struct btree_trans *trans, struct btree_iter *iter)
{
	u8 gen;
	int ret = bch2_check_discard_freespace_key(trans, iter, &gen, false);
	return ret < 0 ? ret : 0;
}

/*
 * We've already checked that generation numbers in the bucket_gens btree are
 * valid for buckets that exist; this just checks for keys for nonexistent
 * buckets.
 */
static noinline_for_stack
int bch2_check_bucket_gens_key(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_i_bucket_gens g;
	u64 start = bucket_gens_pos_to_alloc(k.k->p, 0).offset;
	u64 end = bucket_gens_pos_to_alloc(bpos_nosnap_successor(k.k->p), 0).offset;
	u64 b;
	bool need_update = false;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	BUG_ON(k.k->type != KEY_TYPE_bucket_gens);
	bkey_reassemble(&g.k_i, k);

	struct bch_dev *ca = bch2_dev_tryget_noerror(c, k.k->p.inode);
	if (!ca) {
		if (fsck_err(trans, bucket_gens_to_invalid_dev,
			     "bucket_gens key for invalid device:\n%s",
			     (bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
			ret = bch2_btree_delete_at(trans, iter, 0);
		goto out;
	}

	if (fsck_err_on(end <= ca->mi.first_bucket ||
			start >= ca->mi.nbuckets,
			trans, bucket_gens_to_invalid_buckets,
			"bucket_gens key for invalid buckets:\n%s",
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		ret = bch2_btree_delete_at(trans, iter, 0);
		goto out;
	}

	for (b = start; b < ca->mi.first_bucket; b++)
		if (fsck_err_on(g.v.gens[b & KEY_TYPE_BUCKET_GENS_MASK],
				trans, bucket_gens_nonzero_for_invalid_buckets,
				"bucket_gens key has nonzero gen for invalid bucket")) {
			g.v.gens[b & KEY_TYPE_BUCKET_GENS_MASK] = 0;
			need_update = true;
		}

	for (b = ca->mi.nbuckets; b < end; b++)
		if (fsck_err_on(g.v.gens[b & KEY_TYPE_BUCKET_GENS_MASK],
				trans, bucket_gens_nonzero_for_invalid_buckets,
				"bucket_gens key has nonzero gen for invalid bucket")) {
			g.v.gens[b & KEY_TYPE_BUCKET_GENS_MASK] = 0;
			need_update = true;
		}

	if (need_update) {
		struct bkey_i *u = bch2_trans_kmalloc(trans, sizeof(g));

		ret = PTR_ERR_OR_ZERO(u);
		if (ret)
			goto out;

		memcpy(u, &g, sizeof(g));
		ret = bch2_trans_update(trans, iter, u, 0);
	}
out:
fsck_err:
	bch2_dev_put(ca);
	printbuf_exit(&buf);
	return ret;
}

int bch2_check_alloc_info(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter, discard_iter, freespace_iter, bucket_gens_iter;
	struct bch_dev *ca = NULL;
	struct bkey hole;
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_alloc, POS_MIN,
			     BTREE_ITER_prefetch);
	bch2_trans_iter_init(trans, &discard_iter, BTREE_ID_need_discard, POS_MIN,
			     BTREE_ITER_prefetch);
	bch2_trans_iter_init(trans, &freespace_iter, BTREE_ID_freespace, POS_MIN,
			     BTREE_ITER_prefetch);
	bch2_trans_iter_init(trans, &bucket_gens_iter, BTREE_ID_bucket_gens, POS_MIN,
			     BTREE_ITER_prefetch);

	while (1) {
		struct bpos next;

		bch2_trans_begin(trans);

		k = bch2_get_key_or_real_bucket_hole(&iter, &ca, &hole);
		ret = bkey_err(k);
		if (ret)
			goto bkey_err;

		if (!k.k)
			break;

		if (k.k->type) {
			next = bpos_nosnap_successor(k.k->p);

			ret = bch2_check_alloc_key(trans,
						   k, &iter,
						   &discard_iter,
						   &freespace_iter,
						   &bucket_gens_iter);
			if (ret)
				goto bkey_err;
		} else {
			next = k.k->p;

			ret = bch2_check_alloc_hole_freespace(trans, ca,
						    bkey_start_pos(k.k),
						    &next,
						    &freespace_iter) ?:
				bch2_check_alloc_hole_bucket_gens(trans,
						    bkey_start_pos(k.k),
						    &next,
						    &bucket_gens_iter);
			if (ret)
				goto bkey_err;
		}

		ret = bch2_trans_commit(trans, NULL, NULL,
					BCH_TRANS_COMMIT_no_enospc);
		if (ret)
			goto bkey_err;

		bch2_btree_iter_set_pos(&iter, next);
bkey_err:
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			break;
	}
	bch2_trans_iter_exit(trans, &bucket_gens_iter);
	bch2_trans_iter_exit(trans, &freespace_iter);
	bch2_trans_iter_exit(trans, &discard_iter);
	bch2_trans_iter_exit(trans, &iter);
	bch2_dev_put(ca);
	ca = NULL;

	if (ret < 0)
		goto err;

	ret = for_each_btree_key(trans, iter,
			BTREE_ID_need_discard, POS_MIN,
			BTREE_ITER_prefetch, k,
		bch2_check_discard_freespace_key_fsck(trans, &iter));
	if (ret)
		goto err;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_freespace, POS_MIN,
			     BTREE_ITER_prefetch);
	while (1) {
		bch2_trans_begin(trans);
		k = bch2_btree_iter_peek(&iter);
		if (!k.k)
			break;

		ret = bkey_err(k) ?:
			bch2_check_discard_freespace_key_fsck(trans, &iter);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
			ret = 0;
			continue;
		}
		if (ret) {
			struct printbuf buf = PRINTBUF;
			bch2_bkey_val_to_text(&buf, c, k);

			bch_err(c, "while checking %s", buf.buf);
			printbuf_exit(&buf);
			break;
		}

		bch2_btree_iter_set_pos(&iter, bpos_nosnap_successor(iter.pos));
	}
	bch2_trans_iter_exit(trans, &iter);
	if (ret)
		goto err;

	ret = for_each_btree_key_commit(trans, iter,
			BTREE_ID_bucket_gens, POS_MIN,
			BTREE_ITER_prefetch, k,
			NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
		bch2_check_bucket_gens_key(trans, &iter, k));
err:
	bch2_trans_put(trans);
	bch_err_fn(c, ret);
	return ret;
}

static int bch2_check_alloc_to_lru_ref(struct btree_trans *trans,
				       struct btree_iter *alloc_iter,
				       struct bkey_buf *last_flushed)
{
	struct bch_fs *c = trans->c;
	struct bch_alloc_v4 a_convert;
	const struct bch_alloc_v4 *a;
	struct bkey_s_c alloc_k;
	struct printbuf buf = PRINTBUF;
	int ret;

	alloc_k = bch2_btree_iter_peek(alloc_iter);
	if (!alloc_k.k)
		return 0;

	ret = bkey_err(alloc_k);
	if (ret)
		return ret;

	struct bch_dev *ca = bch2_dev_tryget_noerror(c, alloc_k.k->p.inode);
	if (!ca)
		return 0;

	a = bch2_alloc_to_v4(alloc_k, &a_convert);

	u64 lru_idx = alloc_lru_idx_fragmentation(*a, ca);
	if (lru_idx) {
		ret = bch2_lru_check_set(trans, BCH_LRU_BUCKET_FRAGMENTATION,
					 bucket_to_u64(alloc_k.k->p),
					 lru_idx, alloc_k, last_flushed);
		if (ret)
			goto err;
	}

	if (a->data_type != BCH_DATA_cached)
		goto err;

	if (fsck_err_on(!a->io_time[READ],
			trans, alloc_key_cached_but_read_time_zero,
			"cached bucket with read_time 0\n%s",
		(printbuf_reset(&buf),
		 bch2_bkey_val_to_text(&buf, c, alloc_k), buf.buf))) {
		struct bkey_i_alloc_v4 *a_mut =
			bch2_alloc_to_v4_mut(trans, alloc_k);
		ret = PTR_ERR_OR_ZERO(a_mut);
		if (ret)
			goto err;

		a_mut->v.io_time[READ] = bch2_current_io_time(c, READ);
		ret = bch2_trans_update(trans, alloc_iter,
					&a_mut->k_i, BTREE_TRIGGER_norun);
		if (ret)
			goto err;

		a = &a_mut->v;
	}

	ret = bch2_lru_check_set(trans, alloc_k.k->p.inode,
				 bucket_to_u64(alloc_k.k->p),
				 a->io_time[READ],
				 alloc_k, last_flushed);
	if (ret)
		goto err;
err:
fsck_err:
	bch2_dev_put(ca);
	printbuf_exit(&buf);
	return ret;
}

int bch2_check_alloc_to_lru_refs(struct bch_fs *c)
{
	struct bkey_buf last_flushed;

	bch2_bkey_buf_init(&last_flushed);
	bkey_init(&last_flushed.k->k);

	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_alloc,
				POS_MIN, BTREE_ITER_prefetch, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			bch2_check_alloc_to_lru_ref(trans, &iter, &last_flushed))) ?:
		bch2_check_stripe_to_lru_refs(c);

	bch2_bkey_buf_exit(&last_flushed, c);
	bch_err_fn(c, ret);
	return ret;
}

static int discard_in_flight_add(struct bch_dev *ca, u64 bucket, bool in_progress)
{
	int ret;

	mutex_lock(&ca->discard_buckets_in_flight_lock);
	darray_for_each(ca->discard_buckets_in_flight, i)
		if (i->bucket == bucket) {
			ret = -BCH_ERR_EEXIST_discard_in_flight_add;
			goto out;
		}

	ret = darray_push(&ca->discard_buckets_in_flight, ((struct discard_in_flight) {
			   .in_progress = in_progress,
			   .bucket	= bucket,
	}));
out:
	mutex_unlock(&ca->discard_buckets_in_flight_lock);
	return ret;
}

static void discard_in_flight_remove(struct bch_dev *ca, u64 bucket)
{
	mutex_lock(&ca->discard_buckets_in_flight_lock);
	darray_for_each(ca->discard_buckets_in_flight, i)
		if (i->bucket == bucket) {
			BUG_ON(!i->in_progress);
			darray_remove_item(&ca->discard_buckets_in_flight, i);
			goto found;
		}
	BUG();
found:
	mutex_unlock(&ca->discard_buckets_in_flight_lock);
}

struct discard_buckets_state {
	u64		seen;
	u64		open;
	u64		need_journal_commit;
	u64		discarded;
};

/*
 * This is needed because discard is both a filesystem option and a device
 * option, and mount options are supposed to apply to that mount and not be
 * persisted, i.e. if it's set as a mount option we can't propagate it to the
 * device.
 */
static inline bool discard_opt_enabled(struct bch_fs *c, struct bch_dev *ca)
{
	return test_bit(BCH_FS_discard_mount_opt_set, &c->flags)
		? c->opts.discard
		: ca->mi.discard;
}

static int bch2_discard_one_bucket(struct btree_trans *trans,
				   struct bch_dev *ca,
				   struct btree_iter *need_discard_iter,
				   struct bpos *discard_pos_done,
				   struct discard_buckets_state *s,
				   bool fastpath)
{
	struct bch_fs *c = trans->c;
	struct bpos pos = need_discard_iter->pos;
	struct btree_iter iter = { NULL };
	struct bkey_s_c k;
	struct bkey_i_alloc_v4 *a;
	struct printbuf buf = PRINTBUF;
	bool discard_locked = false;
	int ret = 0;

	if (bch2_bucket_is_open_safe(c, pos.inode, pos.offset)) {
		s->open++;
		goto out;
	}

	u64 seq_ready = bch2_bucket_journal_seq_ready(&c->buckets_waiting_for_journal,
						      pos.inode, pos.offset);
	if (seq_ready > c->journal.flushed_seq_ondisk) {
		if (seq_ready > c->journal.flushing_seq)
			s->need_journal_commit++;
		goto out;
	}

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_alloc,
			       need_discard_iter->pos,
			       BTREE_ITER_cached);
	ret = bkey_err(k);
	if (ret)
		goto out;

	a = bch2_alloc_to_v4_mut(trans, k);
	ret = PTR_ERR_OR_ZERO(a);
	if (ret)
		goto out;

	if (a->v.data_type != BCH_DATA_need_discard) {
		if (need_discard_or_freespace_err(trans, k, true, true, true)) {
			ret = bch2_btree_bit_mod_iter(trans, need_discard_iter, false);
			if (ret)
				goto out;
			goto commit;
		}

		goto out;
	}

	if (!fastpath) {
		if (discard_in_flight_add(ca, iter.pos.offset, true))
			goto out;

		discard_locked = true;
	}

	if (!bkey_eq(*discard_pos_done, iter.pos)) {
		s->discarded++;
		*discard_pos_done = iter.pos;

		if (discard_opt_enabled(c, ca) && !c->opts.nochanges) {
			/*
			 * This works without any other locks because this is the only
			 * thread that removes items from the need_discard tree
			 */
			bch2_trans_unlock_long(trans);
			blkdev_issue_discard(ca->disk_sb.bdev,
					     k.k->p.offset * ca->mi.bucket_size,
					     ca->mi.bucket_size,
					     GFP_KERNEL);
			ret = bch2_trans_relock_notrace(trans);
			if (ret)
				goto out;
		}
	}

	SET_BCH_ALLOC_V4_NEED_DISCARD(&a->v, false);
	alloc_data_type_set(&a->v, a->v.data_type);

	ret = bch2_trans_update(trans, &iter, &a->k_i, 0);
	if (ret)
		goto out;
commit:
	ret = bch2_trans_commit(trans, NULL, NULL,
				BCH_WATERMARK_btree|
				BCH_TRANS_COMMIT_no_enospc);
	if (ret)
		goto out;

	if (!fastpath)
		count_event(c, bucket_discard);
	else
		count_event(c, bucket_discard_fast);
out:
fsck_err:
	if (discard_locked)
		discard_in_flight_remove(ca, iter.pos.offset);
	if (!ret)
		s->seen++;
	bch2_trans_iter_exit(trans, &iter);
	printbuf_exit(&buf);
	return ret;
}

static void bch2_do_discards_work(struct work_struct *work)
{
	struct bch_dev *ca = container_of(work, struct bch_dev, discard_work);
	struct bch_fs *c = ca->fs;
	struct discard_buckets_state s = {};
	struct bpos discard_pos_done = POS_MAX;
	int ret;

	/*
	 * We're doing the commit in bch2_discard_one_bucket instead of using
	 * for_each_btree_key_commit() so that we can increment counters after
	 * successful commit:
	 */
	ret = bch2_trans_run(c,
		for_each_btree_key_max(trans, iter,
				   BTREE_ID_need_discard,
				   POS(ca->dev_idx, 0),
				   POS(ca->dev_idx, U64_MAX), 0, k,
			bch2_discard_one_bucket(trans, ca, &iter, &discard_pos_done, &s, false)));

	if (s.need_journal_commit > dev_buckets_available(ca, BCH_WATERMARK_normal))
		bch2_journal_flush_async(&c->journal, NULL);

	trace_discard_buckets(c, s.seen, s.open, s.need_journal_commit, s.discarded,
			      bch2_err_str(ret));

	percpu_ref_put(&ca->io_ref);
	bch2_write_ref_put(c, BCH_WRITE_REF_discard);
}

void bch2_dev_do_discards(struct bch_dev *ca)
{
	struct bch_fs *c = ca->fs;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_discard))
		return;

	if (!bch2_dev_get_ioref(c, ca->dev_idx, WRITE))
		goto put_write_ref;

	if (queue_work(c->write_ref_wq, &ca->discard_work))
		return;

	percpu_ref_put(&ca->io_ref);
put_write_ref:
	bch2_write_ref_put(c, BCH_WRITE_REF_discard);
}

void bch2_do_discards(struct bch_fs *c)
{
	for_each_member_device(c, ca)
		bch2_dev_do_discards(ca);
}

static int bch2_do_discards_fast_one(struct btree_trans *trans,
				     struct bch_dev *ca,
				     u64 bucket,
				     struct bpos *discard_pos_done,
				     struct discard_buckets_state *s)
{
	struct btree_iter need_discard_iter;
	struct bkey_s_c discard_k = bch2_bkey_get_iter(trans, &need_discard_iter,
					BTREE_ID_need_discard, POS(ca->dev_idx, bucket), 0);
	int ret = bkey_err(discard_k);
	if (ret)
		return ret;

	if (log_fsck_err_on(discard_k.k->type != KEY_TYPE_set,
			    trans, discarding_bucket_not_in_need_discard_btree,
			    "attempting to discard bucket %u:%llu not in need_discard btree",
			    ca->dev_idx, bucket))
		goto out;

	ret = bch2_discard_one_bucket(trans, ca, &need_discard_iter, discard_pos_done, s, true);
out:
fsck_err:
	bch2_trans_iter_exit(trans, &need_discard_iter);
	return ret;
}

static void bch2_do_discards_fast_work(struct work_struct *work)
{
	struct bch_dev *ca = container_of(work, struct bch_dev, discard_fast_work);
	struct bch_fs *c = ca->fs;
	struct discard_buckets_state s = {};
	struct bpos discard_pos_done = POS_MAX;
	struct btree_trans *trans = bch2_trans_get(c);
	int ret = 0;

	while (1) {
		bool got_bucket = false;
		u64 bucket;

		mutex_lock(&ca->discard_buckets_in_flight_lock);
		darray_for_each(ca->discard_buckets_in_flight, i) {
			if (i->in_progress)
				continue;

			got_bucket = true;
			bucket = i->bucket;
			i->in_progress = true;
			break;
		}
		mutex_unlock(&ca->discard_buckets_in_flight_lock);

		if (!got_bucket)
			break;

		ret = lockrestart_do(trans,
			bch2_do_discards_fast_one(trans, ca, bucket, &discard_pos_done, &s));
		bch_err_fn(c, ret);

		discard_in_flight_remove(ca, bucket);

		if (ret)
			break;
	}

	trace_discard_buckets_fast(c, s.seen, s.open, s.need_journal_commit, s.discarded, bch2_err_str(ret));

	bch2_trans_put(trans);
	percpu_ref_put(&ca->io_ref);
	bch2_write_ref_put(c, BCH_WRITE_REF_discard_fast);
}

static void bch2_discard_one_bucket_fast(struct bch_dev *ca, u64 bucket)
{
	struct bch_fs *c = ca->fs;

	if (discard_in_flight_add(ca, bucket, false))
		return;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_discard_fast))
		return;

	if (!bch2_dev_get_ioref(c, ca->dev_idx, WRITE))
		goto put_ref;

	if (queue_work(c->write_ref_wq, &ca->discard_fast_work))
		return;

	percpu_ref_put(&ca->io_ref);
put_ref:
	bch2_write_ref_put(c, BCH_WRITE_REF_discard_fast);
}

static int invalidate_one_bp(struct btree_trans *trans,
			     struct bch_dev *ca,
			     struct bkey_s_c_backpointer bp,
			     struct bkey_buf *last_flushed)
{
	struct btree_iter extent_iter;
	struct bkey_s_c extent_k =
		bch2_backpointer_get_key(trans, bp, &extent_iter, 0, last_flushed);
	int ret = bkey_err(extent_k);
	if (ret)
		return ret;

	struct bkey_i *n =
		bch2_bkey_make_mut(trans, &extent_iter, &extent_k,
				   BTREE_UPDATE_internal_snapshot_node);
	ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		goto err;

	bch2_bkey_drop_device(bkey_i_to_s(n), ca->dev_idx);
err:
	bch2_trans_iter_exit(trans, &extent_iter);
	return ret;
}

static int invalidate_one_bucket_by_bps(struct btree_trans *trans,
					struct bch_dev *ca,
					struct bpos bucket,
					u8 gen,
					struct bkey_buf *last_flushed)
{
	struct bpos bp_start	= bucket_pos_to_bp_start(ca,	bucket);
	struct bpos bp_end	= bucket_pos_to_bp_end(ca,	bucket);

	return for_each_btree_key_max_commit(trans, iter, BTREE_ID_backpointers,
				      bp_start, bp_end, 0, k,
				      NULL, NULL,
				      BCH_WATERMARK_btree|
				      BCH_TRANS_COMMIT_no_enospc, ({
		if (k.k->type != KEY_TYPE_backpointer)
			continue;

		struct bkey_s_c_backpointer bp = bkey_s_c_to_backpointer(k);

		if (bp.v->bucket_gen != gen)
			continue;

		/* filter out bps with gens that don't match */

		invalidate_one_bp(trans, ca, bp, last_flushed);
	}));
}

noinline_for_stack
static int invalidate_one_bucket(struct btree_trans *trans,
				 struct bch_dev *ca,
				 struct btree_iter *lru_iter,
				 struct bkey_s_c lru_k,
				 struct bkey_buf *last_flushed,
				 s64 *nr_to_invalidate)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	struct bpos bucket = u64_to_bucket(lru_k.k->p.offset);
	struct btree_iter alloc_iter = {};
	int ret = 0;

	if (*nr_to_invalidate <= 0)
		return 1;

	if (!bch2_dev_bucket_exists(c, bucket)) {
		if (fsck_err(trans, lru_entry_to_invalid_bucket,
			     "lru key points to nonexistent device:bucket %llu:%llu",
			     bucket.inode, bucket.offset))
			return bch2_btree_bit_mod_buffered(trans, BTREE_ID_lru, lru_iter->pos, false);
		goto out;
	}

	if (bch2_bucket_is_open_safe(c, bucket.inode, bucket.offset))
		return 0;

	struct bkey_s_c alloc_k = bch2_bkey_get_iter(trans, &alloc_iter,
						     BTREE_ID_alloc, bucket,
						     BTREE_ITER_cached);
	ret = bkey_err(alloc_k);
	if (ret)
		return ret;

	struct bch_alloc_v4 a_convert;
	const struct bch_alloc_v4 *a = bch2_alloc_to_v4(alloc_k, &a_convert);

	/* We expect harmless races here due to the btree write buffer: */
	if (lru_pos_time(lru_iter->pos) != alloc_lru_idx_read(*a))
		goto out;

	/*
	 * Impossible since alloc_lru_idx_read() only returns nonzero if the
	 * bucket is supposed to be on the cached bucket LRU (i.e.
	 * BCH_DATA_cached)
	 *
	 * bch2_lru_validate() also disallows lru keys with lru_pos_time() == 0
	 */
	BUG_ON(a->data_type != BCH_DATA_cached);
	BUG_ON(a->dirty_sectors);

	if (!a->cached_sectors)
		bch_err(c, "invalidating empty bucket, confused");

	unsigned cached_sectors = a->cached_sectors;
	u8 gen = a->gen;

	ret = invalidate_one_bucket_by_bps(trans, ca, bucket, gen, last_flushed);
	if (ret)
		goto out;

	trace_and_count(c, bucket_invalidate, c, bucket.inode, bucket.offset, cached_sectors);
	--*nr_to_invalidate;
out:
fsck_err:
	bch2_trans_iter_exit(trans, &alloc_iter);
	printbuf_exit(&buf);
	return ret;
}

static struct bkey_s_c next_lru_key(struct btree_trans *trans, struct btree_iter *iter,
				    struct bch_dev *ca, bool *wrapped)
{
	struct bkey_s_c k;
again:
	k = bch2_btree_iter_peek_max(iter, lru_pos(ca->dev_idx, U64_MAX, LRU_TIME_MAX));
	if (!k.k && !*wrapped) {
		bch2_btree_iter_set_pos(iter, lru_pos(ca->dev_idx, 0, 0));
		*wrapped = true;
		goto again;
	}

	return k;
}

static void bch2_do_invalidates_work(struct work_struct *work)
{
	struct bch_dev *ca = container_of(work, struct bch_dev, invalidate_work);
	struct bch_fs *c = ca->fs;
	struct btree_trans *trans = bch2_trans_get(c);
	int ret = 0;

	struct bkey_buf last_flushed;
	bch2_bkey_buf_init(&last_flushed);
	bkey_init(&last_flushed.k->k);

	ret = bch2_btree_write_buffer_tryflush(trans);
	if (ret)
		goto err;

	s64 nr_to_invalidate =
		should_invalidate_buckets(ca, bch2_dev_usage_read(ca));
	struct btree_iter iter;
	bool wrapped = false;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_lru,
			     lru_pos(ca->dev_idx, 0,
				     ((bch2_current_io_time(c, READ) + U32_MAX) &
				      LRU_TIME_MAX)), 0);

	while (true) {
		bch2_trans_begin(trans);

		struct bkey_s_c k = next_lru_key(trans, &iter, ca, &wrapped);
		ret = bkey_err(k);
		if (ret)
			goto restart_err;
		if (!k.k)
			break;

		ret = invalidate_one_bucket(trans, ca, &iter, k, &last_flushed, &nr_to_invalidate);
restart_err:
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			break;

		bch2_btree_iter_advance(&iter);
	}
	bch2_trans_iter_exit(trans, &iter);
err:
	bch2_trans_put(trans);
	percpu_ref_put(&ca->io_ref);
	bch2_bkey_buf_exit(&last_flushed, c);
	bch2_write_ref_put(c, BCH_WRITE_REF_invalidate);
}

void bch2_dev_do_invalidates(struct bch_dev *ca)
{
	struct bch_fs *c = ca->fs;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_invalidate))
		return;

	if (!bch2_dev_get_ioref(c, ca->dev_idx, WRITE))
		goto put_ref;

	if (queue_work(c->write_ref_wq, &ca->invalidate_work))
		return;

	percpu_ref_put(&ca->io_ref);
put_ref:
	bch2_write_ref_put(c, BCH_WRITE_REF_invalidate);
}

void bch2_do_invalidates(struct bch_fs *c)
{
	for_each_member_device(c, ca)
		bch2_dev_do_invalidates(ca);
}

int bch2_dev_freespace_init(struct bch_fs *c, struct bch_dev *ca,
			    u64 bucket_start, u64 bucket_end)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey hole;
	struct bpos end = POS(ca->dev_idx, bucket_end);
	struct bch_member *m;
	unsigned long last_updated = jiffies;
	int ret;

	BUG_ON(bucket_start > bucket_end);
	BUG_ON(bucket_end > ca->mi.nbuckets);

	bch2_trans_iter_init(trans, &iter, BTREE_ID_alloc,
		POS(ca->dev_idx, max_t(u64, ca->mi.first_bucket, bucket_start)),
		BTREE_ITER_prefetch);
	/*
	 * Scan the alloc btree for every bucket on @ca, and add buckets to the
	 * freespace/need_discard/need_gc_gens btrees as needed:
	 */
	while (1) {
		if (time_after(jiffies, last_updated + HZ * 10)) {
			bch_info(ca, "%s: currently at %llu/%llu",
				 __func__, iter.pos.offset, ca->mi.nbuckets);
			last_updated = jiffies;
		}

		bch2_trans_begin(trans);

		if (bkey_ge(iter.pos, end)) {
			ret = 0;
			break;
		}

		k = bch2_get_key_or_hole(&iter, end, &hole);
		ret = bkey_err(k);
		if (ret)
			goto bkey_err;

		if (k.k->type) {
			/*
			 * We process live keys in the alloc btree one at a
			 * time:
			 */
			struct bch_alloc_v4 a_convert;
			const struct bch_alloc_v4 *a = bch2_alloc_to_v4(k, &a_convert);

			ret =   bch2_bucket_do_index(trans, ca, k, a, true) ?:
				bch2_trans_commit(trans, NULL, NULL,
						  BCH_TRANS_COMMIT_no_enospc);
			if (ret)
				goto bkey_err;

			bch2_btree_iter_advance(&iter);
		} else {
			struct bkey_i *freespace;

			freespace = bch2_trans_kmalloc(trans, sizeof(*freespace));
			ret = PTR_ERR_OR_ZERO(freespace);
			if (ret)
				goto bkey_err;

			bkey_init(&freespace->k);
			freespace->k.type	= KEY_TYPE_set;
			freespace->k.p		= k.k->p;
			freespace->k.size	= k.k->size;

			ret = bch2_btree_insert_trans(trans, BTREE_ID_freespace, freespace, 0) ?:
				bch2_trans_commit(trans, NULL, NULL,
						  BCH_TRANS_COMMIT_no_enospc);
			if (ret)
				goto bkey_err;

			bch2_btree_iter_set_pos(&iter, k.k->p);
		}
bkey_err:
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			break;
	}

	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);

	if (ret < 0) {
		bch_err_msg(ca, ret, "initializing free space");
		return ret;
	}

	mutex_lock(&c->sb_lock);
	m = bch2_members_v2_get_mut(c->disk_sb.sb, ca->dev_idx);
	SET_BCH_MEMBER_FREESPACE_INITIALIZED(m, true);
	mutex_unlock(&c->sb_lock);

	return 0;
}

int bch2_fs_freespace_init(struct bch_fs *c)
{
	int ret = 0;
	bool doing_init = false;

	/*
	 * We can crash during the device add path, so we need to check this on
	 * every mount:
	 */

	for_each_member_device(c, ca) {
		if (ca->mi.freespace_initialized)
			continue;

		if (!doing_init) {
			bch_info(c, "initializing freespace");
			doing_init = true;
		}

		ret = bch2_dev_freespace_init(c, ca, 0, ca->mi.nbuckets);
		if (ret) {
			bch2_dev_put(ca);
			bch_err_fn(c, ret);
			return ret;
		}
	}

	if (doing_init) {
		mutex_lock(&c->sb_lock);
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
		bch_verbose(c, "done initializing freespace");
	}

	return 0;
}

/* device removal */

int bch2_dev_remove_alloc(struct bch_fs *c, struct bch_dev *ca)
{
	struct bpos start	= POS(ca->dev_idx, 0);
	struct bpos end		= POS(ca->dev_idx, U64_MAX);
	int ret;

	/*
	 * We clear the LRU and need_discard btrees first so that we don't race
	 * with bch2_do_invalidates() and bch2_do_discards()
	 */
	ret =   bch2_dev_remove_stripes(c, ca->dev_idx) ?:
		bch2_btree_delete_range(c, BTREE_ID_lru, start, end,
					BTREE_TRIGGER_norun, NULL) ?:
		bch2_btree_delete_range(c, BTREE_ID_need_discard, start, end,
					BTREE_TRIGGER_norun, NULL) ?:
		bch2_btree_delete_range(c, BTREE_ID_freespace, start, end,
					BTREE_TRIGGER_norun, NULL) ?:
		bch2_btree_delete_range(c, BTREE_ID_backpointers, start, end,
					BTREE_TRIGGER_norun, NULL) ?:
		bch2_btree_delete_range(c, BTREE_ID_bucket_gens, start, end,
					BTREE_TRIGGER_norun, NULL) ?:
		bch2_btree_delete_range(c, BTREE_ID_alloc, start, end,
					BTREE_TRIGGER_norun, NULL) ?:
		bch2_dev_usage_remove(c, ca->dev_idx);
	bch_err_msg(ca, ret, "removing dev alloc info");
	return ret;
}

/* Bucket IO clocks: */

static int __bch2_bucket_io_time_reset(struct btree_trans *trans, unsigned dev,
				size_t bucket_nr, int rw)
{
	struct bch_fs *c = trans->c;

	struct btree_iter iter;
	struct bkey_i_alloc_v4 *a =
		bch2_trans_start_alloc_update_noupdate(trans, &iter, POS(dev, bucket_nr));
	int ret = PTR_ERR_OR_ZERO(a);
	if (ret)
		return ret;

	u64 now = bch2_current_io_time(c, rw);
	if (a->v.io_time[rw] == now)
		goto out;

	a->v.io_time[rw] = now;

	ret   = bch2_trans_update(trans, &iter, &a->k_i, 0) ?:
		bch2_trans_commit(trans, NULL, NULL, 0);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_bucket_io_time_reset(struct btree_trans *trans, unsigned dev,
			      size_t bucket_nr, int rw)
{
	if (bch2_trans_relock(trans))
		bch2_trans_begin(trans);

	return nested_lockrestart_do(trans, __bch2_bucket_io_time_reset(trans, dev, bucket_nr, rw));
}

/* Startup/shutdown (ro/rw): */

void bch2_recalc_capacity(struct bch_fs *c)
{
	u64 capacity = 0, reserved_sectors = 0, gc_reserve;
	unsigned bucket_size_max = 0;
	unsigned long ra_pages = 0;

	lockdep_assert_held(&c->state_lock);

	for_each_online_member(c, ca) {
		struct backing_dev_info *bdi = ca->disk_sb.bdev->bd_disk->bdi;

		ra_pages += bdi->ra_pages;
	}

	bch2_set_ra_pages(c, ra_pages);

	for_each_rw_member(c, ca) {
		u64 dev_reserve = 0;

		/*
		 * We need to reserve buckets (from the number
		 * of currently available buckets) against
		 * foreground writes so that mainly copygc can
		 * make forward progress.
		 *
		 * We need enough to refill the various reserves
		 * from scratch - copygc will use its entire
		 * reserve all at once, then run against when
		 * its reserve is refilled (from the formerly
		 * available buckets).
		 *
		 * This reserve is just used when considering if
		 * allocations for foreground writes must wait -
		 * not -ENOSPC calculations.
		 */

		dev_reserve += ca->nr_btree_reserve * 2;
		dev_reserve += ca->mi.nbuckets >> 6; /* copygc reserve */

		dev_reserve += 1;	/* btree write point */
		dev_reserve += 1;	/* copygc write point */
		dev_reserve += 1;	/* rebalance write point */

		dev_reserve *= ca->mi.bucket_size;

		capacity += bucket_to_sector(ca, ca->mi.nbuckets -
					     ca->mi.first_bucket);

		reserved_sectors += dev_reserve * 2;

		bucket_size_max = max_t(unsigned, bucket_size_max,
					ca->mi.bucket_size);
	}

	gc_reserve = c->opts.gc_reserve_bytes
		? c->opts.gc_reserve_bytes >> 9
		: div64_u64(capacity * c->opts.gc_reserve_percent, 100);

	reserved_sectors = max(gc_reserve, reserved_sectors);

	reserved_sectors = min(reserved_sectors, capacity);

	c->reserved = reserved_sectors;
	c->capacity = capacity - reserved_sectors;

	c->bucket_size_max = bucket_size_max;

	/* Wake up case someone was waiting for buckets */
	closure_wake_up(&c->freelist_wait);
}

u64 bch2_min_rw_member_capacity(struct bch_fs *c)
{
	u64 ret = U64_MAX;

	for_each_rw_member(c, ca)
		ret = min(ret, ca->mi.nbuckets * ca->mi.bucket_size);
	return ret;
}

static bool bch2_dev_has_open_write_point(struct bch_fs *c, struct bch_dev *ca)
{
	struct open_bucket *ob;
	bool ret = false;

	for (ob = c->open_buckets;
	     ob < c->open_buckets + ARRAY_SIZE(c->open_buckets);
	     ob++) {
		spin_lock(&ob->lock);
		if (ob->valid && !ob->on_partial_list &&
		    ob->dev == ca->dev_idx)
			ret = true;
		spin_unlock(&ob->lock);
	}

	return ret;
}

/* device goes ro: */
void bch2_dev_allocator_remove(struct bch_fs *c, struct bch_dev *ca)
{
	lockdep_assert_held(&c->state_lock);

	/* First, remove device from allocation groups: */

	for (unsigned i = 0; i < ARRAY_SIZE(c->rw_devs); i++)
		clear_bit(ca->dev_idx, c->rw_devs[i].d);

	c->rw_devs_change_count++;

	/*
	 * Capacity is calculated based off of devices in allocation groups:
	 */
	bch2_recalc_capacity(c);

	bch2_open_buckets_stop(c, ca, false);

	/*
	 * Wake up threads that were blocked on allocation, so they can notice
	 * the device can no longer be removed and the capacity has changed:
	 */
	closure_wake_up(&c->freelist_wait);

	/*
	 * journal_res_get() can block waiting for free space in the journal -
	 * it needs to notice there may not be devices to allocate from anymore:
	 */
	wake_up(&c->journal.wait);

	/* Now wait for any in flight writes: */

	closure_wait_event(&c->open_buckets_wait,
			   !bch2_dev_has_open_write_point(c, ca));
}

/* device goes rw: */
void bch2_dev_allocator_add(struct bch_fs *c, struct bch_dev *ca)
{
	lockdep_assert_held(&c->state_lock);

	for (unsigned i = 0; i < ARRAY_SIZE(c->rw_devs); i++)
		if (ca->mi.data_allowed & (1 << i))
			set_bit(ca->dev_idx, c->rw_devs[i].d);

	c->rw_devs_change_count++;
}

void bch2_dev_allocator_background_exit(struct bch_dev *ca)
{
	darray_exit(&ca->discard_buckets_in_flight);
}

void bch2_dev_allocator_background_init(struct bch_dev *ca)
{
	mutex_init(&ca->discard_buckets_in_flight_lock);
	INIT_WORK(&ca->discard_work, bch2_do_discards_work);
	INIT_WORK(&ca->discard_fast_work, bch2_do_discards_fast_work);
	INIT_WORK(&ca->invalidate_work, bch2_do_invalidates_work);
}

void bch2_fs_allocator_background_init(struct bch_fs *c)
{
	spin_lock_init(&c->freelist_lock);
}
