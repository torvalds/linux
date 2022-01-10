// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "btree_cache.h"
#include "btree_io.h"
#include "btree_key_cache.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_gc.h"
#include "buckets.h"
#include "buckets_waiting_for_journal.h"
#include "clock.h"
#include "debug.h"
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

/* Persistent alloc info: */

static const unsigned BCH_ALLOC_V1_FIELD_BYTES[] = {
#define x(name, bits) [BCH_ALLOC_FIELD_V1_##name] = bits / 8,
	BCH_ALLOC_FIELDS_V1()
#undef x
};

const char * const bch2_bucket_states[] = {
	"free",
	"need gc gens",
	"need discard",
	"cached",
	"dirty",
	NULL
};

struct bkey_alloc_unpacked {
	u64		journal_seq;
	u64		bucket;
	u8		dev;
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

static inline void alloc_field_v1_put(struct bkey_i_alloc *a, void **p,
				      unsigned field, u64 v)
{
	unsigned bytes = BCH_ALLOC_V1_FIELD_BYTES[field];

	if (!v)
		return;

	a->v.fields |= 1 << field;

	switch (bytes) {
	case 1:
		*((u8 *) *p) = v;
		break;
	case 2:
		*((__le16 *) *p) = cpu_to_le16(v);
		break;
	case 4:
		*((__le32 *) *p) = cpu_to_le32(v);
		break;
	case 8:
		*((__le64 *) *p) = cpu_to_le64(v);
		break;
	default:
		BUG();
	}

	*p += bytes;
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
	struct bkey_alloc_unpacked ret = {
		.dev	= k.k->p.inode,
		.bucket	= k.k->p.offset,
		.gen	= 0,
	};

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

void bch2_alloc_to_v4(struct bkey_s_c k, struct bch_alloc_v4 *out)
{
	if (k.k->type == KEY_TYPE_alloc_v4) {
		*out = *bkey_s_c_to_alloc_v4(k).v;
	} else {
		struct bkey_alloc_unpacked u = bch2_alloc_unpack(k);

		*out = (struct bch_alloc_v4) {
			.journal_seq		= u.journal_seq,
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
	}
}

struct bkey_i_alloc_v4 *bch2_alloc_to_v4_mut(struct btree_trans *trans, struct bkey_s_c k)
{
	struct bkey_i_alloc_v4 *ret;

	if (k.k->type == KEY_TYPE_alloc_v4) {
		ret = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
		if (!IS_ERR(ret))
			bkey_reassemble(&ret->k_i, k);
	} else {
		ret = bch2_trans_kmalloc(trans, sizeof(*ret));
		if (!IS_ERR(ret)) {
			bkey_alloc_v4_init(&ret->k_i);
			ret->k.p = k.k->p;
			bch2_alloc_to_v4(k, &ret->v);
		}
	}
	return ret;
}

struct bkey_i_alloc_v4 *
bch2_trans_start_alloc_update(struct btree_trans *trans, struct btree_iter *iter,
			      struct bpos pos)
{
	struct bkey_s_c k;
	struct bkey_i_alloc_v4 *a;
	int ret;

	bch2_trans_iter_init(trans, iter, BTREE_ID_alloc, pos,
			     BTREE_ITER_WITH_UPDATES|
			     BTREE_ITER_CACHED|
			     BTREE_ITER_INTENT);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret) {
		bch2_trans_iter_exit(trans, iter);
		return ERR_PTR(ret);
	}

	a = bch2_alloc_to_v4_mut(trans, k);
	if (IS_ERR(a))
		bch2_trans_iter_exit(trans, iter);
	return a;
}

static unsigned bch_alloc_v1_val_u64s(const struct bch_alloc *a)
{
	unsigned i, bytes = offsetof(struct bch_alloc, data);

	for (i = 0; i < ARRAY_SIZE(BCH_ALLOC_V1_FIELD_BYTES); i++)
		if (a->fields & (1 << i))
			bytes += BCH_ALLOC_V1_FIELD_BYTES[i];

	return DIV_ROUND_UP(bytes, sizeof(u64));
}

const char *bch2_alloc_v1_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_alloc a = bkey_s_c_to_alloc(k);

	if (k.k->p.inode >= c->sb.nr_devices ||
	    !c->devs[k.k->p.inode])
		return "invalid device";

	/* allow for unknown fields */
	if (bkey_val_u64s(a.k) < bch_alloc_v1_val_u64s(a.v))
		return "incorrect value size";

	return NULL;
}

const char *bch2_alloc_v2_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_alloc_unpacked u;

	if (k.k->p.inode >= c->sb.nr_devices ||
	    !c->devs[k.k->p.inode])
		return "invalid device";

	if (bch2_alloc_unpack_v2(&u, k))
		return "unpack error";

	return NULL;
}

const char *bch2_alloc_v3_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_alloc_unpacked u;
	struct bch_dev *ca;

	if (k.k->p.inode >= c->sb.nr_devices ||
	    !c->devs[k.k->p.inode])
		return "invalid device";

	ca = bch_dev_bkey_exists(c, k.k->p.inode);

	if (k.k->p.offset < ca->mi.first_bucket ||
	    k.k->p.offset >= ca->mi.nbuckets)
		return "invalid bucket";

	if (bch2_alloc_unpack_v3(&u, k))
		return "unpack error";

	return NULL;
}

const char *bch2_alloc_v4_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bch_dev *ca;

	if (k.k->p.inode >= c->sb.nr_devices ||
	    !c->devs[k.k->p.inode])
		return "invalid device";

	ca = bch_dev_bkey_exists(c, k.k->p.inode);

	if (k.k->p.offset < ca->mi.first_bucket ||
	    k.k->p.offset >= ca->mi.nbuckets)
		return "invalid bucket";

	return NULL;
}

void bch2_alloc_v4_swab(struct bkey_s k)
{
	struct bch_alloc_v4 *a = bkey_s_to_alloc_v4(k).v;

	a->journal_seq		= swab64(a->journal_seq);
	a->flags		= swab32(a->flags);
	a->dirty_sectors	= swab32(a->dirty_sectors);
	a->cached_sectors	= swab32(a->cached_sectors);
	a->io_time[0]		= swab64(a->io_time[0]);
	a->io_time[1]		= swab64(a->io_time[1]);
	a->stripe		= swab32(a->stripe);
	a->nr_external_backpointers = swab32(a->nr_external_backpointers);
}

void bch2_alloc_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bch_alloc_v4 a;

	bch2_alloc_to_v4(k, &a);

	pr_buf(out, "gen %u oldest_gen %u data_type %s journal_seq %llu need_discard %llu",
	       a.gen, a.oldest_gen, bch2_data_types[a.data_type],
	       a.journal_seq, BCH_ALLOC_V4_NEED_DISCARD(&a));
	pr_buf(out, " dirty_sectors %u",	a.dirty_sectors);
	pr_buf(out, " cached_sectors %u",	a.cached_sectors);
	pr_buf(out, " stripe %u",		a.stripe);
	pr_buf(out, " stripe_redundancy %u",	a.stripe_redundancy);
	pr_buf(out, " read_time %llu",		a.io_time[READ]);
	pr_buf(out, " write_time %llu",		a.io_time[WRITE]);
}

int bch2_alloc_read(struct bch_fs *c, bool gc, bool metadata_only)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_alloc_v4 a;
	struct bch_dev *ca;
	struct bucket *g;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_alloc, POS_MIN,
			   BTREE_ITER_PREFETCH, k, ret) {
		ca = bch_dev_bkey_exists(c, k.k->p.inode);
		g = __bucket(ca, k.k->p.offset, gc);
		bch2_alloc_to_v4(k, &a);

		if (!gc)
			*bucket_gen(ca, k.k->p.offset) = a.gen;

		g->_mark.gen		= a.gen;
		g->io_time[READ]	= a.io_time[READ];
		g->io_time[WRITE]	= a.io_time[WRITE];
		g->gen_valid		= 1;

		if (!gc ||
		    (metadata_only &&
		     (a.data_type == BCH_DATA_user ||
		      a.data_type == BCH_DATA_cached ||
		      a.data_type == BCH_DATA_parity))) {
			g->_mark.data_type	= a.data_type;
			g->_mark.dirty_sectors	= a.dirty_sectors;
			g->_mark.cached_sectors	= a.cached_sectors;
			g->_mark.stripe		= a.stripe != 0;
			g->stripe		= a.stripe;
			g->stripe_redundancy	= a.stripe_redundancy;
		}

	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);

	if (ret)
		bch_err(c, "error reading alloc info: %i", ret);

	return ret;
}

/* Free space/discard btree: */

static int bch2_bucket_do_index(struct btree_trans *trans,
				struct bkey_s_c alloc_k,
				struct bch_alloc_v4 a,
				bool set)
{
	struct bch_fs *c = trans->c;
	struct bch_dev *ca = bch_dev_bkey_exists(c, alloc_k.k->p.inode);
	struct btree_iter iter;
	struct bkey_s_c old;
	struct bkey_i *k;
	enum bucket_state state = bucket_state(a);
	enum btree_id btree;
	enum bch_bkey_type old_type = !set ? KEY_TYPE_set : KEY_TYPE_deleted;
	enum bch_bkey_type new_type =  set ? KEY_TYPE_set : KEY_TYPE_deleted;
	struct printbuf buf = PRINTBUF;
	int ret;

	if (state != BUCKET_free &&
	    state != BUCKET_need_discard)
		return 0;

	k = bch2_trans_kmalloc(trans, sizeof(*k));
	if (IS_ERR(k))
		return PTR_ERR(k);

	bkey_init(&k->k);
	k->k.type = new_type;

	switch (state) {
	case BUCKET_free:
		btree = BTREE_ID_freespace;
		k->k.p = alloc_freespace_pos(alloc_k.k->p, a);
		bch2_key_resize(&k->k, 1);
		break;
	case BUCKET_need_discard:
		btree = BTREE_ID_need_discard;
		k->k.p = alloc_k.k->p;
		break;
	default:
		return 0;
	}

	bch2_trans_iter_init(trans, &iter, btree,
			     bkey_start_pos(&k->k),
			     BTREE_ITER_INTENT);
	old = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(old);
	if (ret)
		goto err;

	if (ca->mi.freespace_initialized &&
	    bch2_fs_inconsistent_on(old.k->type != old_type, c,
			"incorrect key when %s %s btree (got %s should be %s)\n"
			"  for %s",
			set ? "setting" : "clearing",
			bch2_btree_ids[btree],
			bch2_bkey_types[old.k->type],
			bch2_bkey_types[old_type],
			(bch2_bkey_val_to_text(&buf, c, alloc_k), buf.buf))) {
		ret = -EIO;
		goto err;
	}

	ret = bch2_trans_update(trans, &iter, k, 0);
err:
	bch2_trans_iter_exit(trans, &iter);
	printbuf_exit(&buf);
	return ret;
}

int bch2_trans_mark_alloc(struct btree_trans *trans,
			  struct bkey_s_c old, struct bkey_i *new,
			  unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bch_alloc_v4 old_a, *new_a;
	u64 old_lru, new_lru;
	int ret = 0;

	/*
	 * Deletion only happens in the device removal path, with
	 * BTREE_TRIGGER_NORUN:
	 */
	BUG_ON(new->k.type != KEY_TYPE_alloc_v4);

	bch2_alloc_to_v4(old, &old_a);
	new_a = &bkey_i_to_alloc_v4(new)->v;

	if (new_a->dirty_sectors > old_a.dirty_sectors ||
	    new_a->cached_sectors > old_a.cached_sectors) {
		new_a->io_time[READ] = max_t(u64, 1, atomic64_read(&c->io_clock[READ].now));
		new_a->io_time[WRITE]= max_t(u64, 1, atomic64_read(&c->io_clock[WRITE].now));
		SET_BCH_ALLOC_V4_NEED_INC_GEN(new_a, true);
	}

	if (old_a.data_type && !new_a->data_type &&
	    old_a.gen == new_a->gen &&
	    !bch2_bucket_is_open_safe(c, new->k.p.inode, new->k.p.offset)) {
		new_a->gen++;
		SET_BCH_ALLOC_V4_NEED_INC_GEN(new_a, false);
	}

	if (bucket_state(old_a) != bucket_state(*new_a) ||
	    (bucket_state(*new_a) == BUCKET_free &&
	     alloc_freespace_genbits(old_a) != alloc_freespace_genbits(*new_a))) {
		ret =   bch2_bucket_do_index(trans, old, old_a, false) ?:
			bch2_bucket_do_index(trans, bkey_i_to_s_c(new), *new_a, true);
		if (ret)
			return ret;
	}

	old_lru = alloc_lru_idx(old_a);
	new_lru = alloc_lru_idx(*new_a);

	if (old_lru != new_lru) {
		ret = bch2_lru_change(trans, new->k.p.inode, new->k.p.offset,
				      old_lru, &new_lru);
		if (ret)
			return ret;

		if (new_lru && new_a->io_time[READ] != new_lru)
			new_a->io_time[READ] = new_lru;
	}

	return 0;
}

static int bch2_dev_freespace_init(struct bch_fs *c, struct bch_dev *ca)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_alloc_v4 a;
	struct bch_member *m;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_alloc,
			   POS(ca->dev_idx, ca->mi.first_bucket),
			   BTREE_ITER_SLOTS|
			   BTREE_ITER_PREFETCH, k, ret) {
		if (iter.pos.offset >= ca->mi.nbuckets)
			break;

		bch2_alloc_to_v4(k, &a);
		ret = __bch2_trans_do(&trans, NULL, NULL,
				      BTREE_INSERT_LAZY_RW,
				 bch2_bucket_do_index(&trans, k, a, true));
		if (ret)
			break;
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);

	if (ret) {
		bch_err(ca, "error initializing free space: %i", ret);
		return ret;
	}

	mutex_lock(&c->sb_lock);
	m = bch2_sb_get_members(c->disk_sb.sb)->members + ca->dev_idx;
	SET_BCH_MEMBER_FREESPACE_INITIALIZED(m, true);
	mutex_unlock(&c->sb_lock);

	return ret;
}

int bch2_fs_freespace_init(struct bch_fs *c)
{
	struct bch_dev *ca;
	unsigned i;
	int ret = 0;
	bool doing_init = false;

	/*
	 * We can crash during the device add path, so we need to check this on
	 * every mount:
	 */

	for_each_member_device(ca, c, i) {
		if (ca->mi.freespace_initialized)
			continue;

		if (!doing_init) {
			bch_info(c, "initializing freespace");
			doing_init = true;
		}

		ret = bch2_dev_freespace_init(c, ca);
		if (ret) {
			percpu_ref_put(&ca->ref);
			return ret;
		}
	}

	if (doing_init) {
		mutex_lock(&c->sb_lock);
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);

		bch_verbose(c, "done initializing freespace");
	}

	return ret;
}

/* Bucket IO clocks: */

int bch2_bucket_io_time_reset(struct btree_trans *trans, unsigned dev,
			      size_t bucket_nr, int rw)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_i_alloc_v4 *a;
	u64 now;
	int ret = 0;

	a = bch2_trans_start_alloc_update(trans, &iter,  POS(dev, bucket_nr));
	ret = PTR_ERR_OR_ZERO(a);
	if (ret)
		return ret;

	now = atomic64_read(&c->io_clock[rw].now);
	if (a->v.io_time[rw] == now)
		goto out;

	a->v.io_time[rw] = now;

	ret   = bch2_trans_update(trans, &iter, &a->k_i, 0) ?:
		bch2_trans_commit(trans, NULL, NULL, 0);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/* Startup/shutdown (ro/rw): */

void bch2_recalc_capacity(struct bch_fs *c)
{
	struct bch_dev *ca;
	u64 capacity = 0, reserved_sectors = 0, gc_reserve;
	unsigned bucket_size_max = 0;
	unsigned long ra_pages = 0;
	unsigned i;

	lockdep_assert_held(&c->state_lock);

	for_each_online_member(ca, c, i) {
		struct backing_dev_info *bdi = ca->disk_sb.bdev->bd_disk->bdi;

		ra_pages += bdi->ra_pages;
	}

	bch2_set_ra_pages(c, ra_pages);

	for_each_rw_member(ca, c, i) {
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

	c->capacity = capacity - reserved_sectors;

	c->bucket_size_max = bucket_size_max;

	/* Wake up case someone was waiting for buckets */
	closure_wake_up(&c->freelist_wait);
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
	unsigned i;

	/* First, remove device from allocation groups: */

	for (i = 0; i < ARRAY_SIZE(c->rw_devs); i++)
		clear_bit(ca->dev_idx, c->rw_devs[i].d);

	/*
	 * Capacity is calculated based off of devices in allocation groups:
	 */
	bch2_recalc_capacity(c);

	/* Next, close write points that point to this device... */
	for (i = 0; i < ARRAY_SIZE(c->write_points); i++)
		bch2_writepoint_stop(c, ca, &c->write_points[i]);

	bch2_writepoint_stop(c, ca, &c->copygc_write_point);
	bch2_writepoint_stop(c, ca, &c->rebalance_write_point);
	bch2_writepoint_stop(c, ca, &c->btree_write_point);

	mutex_lock(&c->btree_reserve_cache_lock);
	while (c->btree_reserve_cache_nr) {
		struct btree_alloc *a =
			&c->btree_reserve_cache[--c->btree_reserve_cache_nr];

		bch2_open_buckets_put(c, &a->ob);
	}
	mutex_unlock(&c->btree_reserve_cache_lock);

	while (1) {
		struct open_bucket *ob;

		spin_lock(&c->freelist_lock);
		if (!ca->open_buckets_partial_nr) {
			spin_unlock(&c->freelist_lock);
			break;
		}
		ob = c->open_buckets +
			ca->open_buckets_partial[--ca->open_buckets_partial_nr];
		ob->on_partial_list = false;
		spin_unlock(&c->freelist_lock);

		bch2_open_bucket_put(c, ob);
	}

	bch2_ec_stop_dev(c, ca);

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
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(c->rw_devs); i++)
		if (ca->mi.data_allowed & (1 << i))
			set_bit(ca->dev_idx, c->rw_devs[i].d);
}

void bch2_fs_allocator_background_init(struct bch_fs *c)
{
	spin_lock_init(&c->freelist_lock);
}
