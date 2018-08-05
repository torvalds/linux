// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Kent Overstreet <kent.overstreet@gmail.com>
 *
 * Code for managing the extent btree and dynamically updating the writeback
 * dirty sector count.
 */

#include "bcachefs.h"
#include "bkey_methods.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "buckets.h"
#include "checksum.h"
#include "debug.h"
#include "dirent.h"
#include "disk_groups.h"
#include "error.h"
#include "extents.h"
#include "inode.h"
#include "journal.h"
#include "replicas.h"
#include "super.h"
#include "super-io.h"
#include "trace.h"
#include "util.h"
#include "xattr.h"

static void sort_key_next(struct btree_node_iter_large *iter,
			  struct btree *b,
			  struct btree_node_iter_set *i)
{
	i->k += __btree_node_offset_to_key(b, i->k)->u64s;

	if (i->k == i->end)
		*i = iter->data[--iter->used];
}

/*
 * Returns true if l > r - unless l == r, in which case returns true if l is
 * older than r.
 *
 * Necessary for btree_sort_fixup() - if there are multiple keys that compare
 * equal in different sets, we have to process them newest to oldest.
 */
#define key_sort_cmp(h, l, r)						\
({									\
	bkey_cmp_packed(b,						\
			__btree_node_offset_to_key(b, (l).k),		\
			__btree_node_offset_to_key(b, (r).k))		\
									\
	?: (l).k - (r).k;						\
})

static inline bool should_drop_next_key(struct btree_node_iter_large *iter,
					struct btree *b)
{
	struct btree_node_iter_set *l = iter->data, *r = iter->data + 1;
	struct bkey_packed *k = __btree_node_offset_to_key(b, l->k);

	if (bkey_whiteout(k))
		return true;

	if (iter->used < 2)
		return false;

	if (iter->used > 2 &&
	    key_sort_cmp(iter, r[0], r[1]) >= 0)
		r++;

	/*
	 * key_sort_cmp() ensures that when keys compare equal the older key
	 * comes first; so if l->k compares equal to r->k then l->k is older and
	 * should be dropped.
	 */
	return !bkey_cmp_packed(b,
				__btree_node_offset_to_key(b, l->k),
				__btree_node_offset_to_key(b, r->k));
}

struct btree_nr_keys bch2_key_sort_fix_overlapping(struct bset *dst,
					struct btree *b,
					struct btree_node_iter_large *iter)
{
	struct bkey_packed *out = dst->start;
	struct btree_nr_keys nr;

	memset(&nr, 0, sizeof(nr));

	heap_resort(iter, key_sort_cmp);

	while (!bch2_btree_node_iter_large_end(iter)) {
		if (!should_drop_next_key(iter, b)) {
			struct bkey_packed *k =
				__btree_node_offset_to_key(b, iter->data->k);

			bkey_copy(out, k);
			btree_keys_account_key_add(&nr, 0, out);
			out = bkey_next(out);
		}

		sort_key_next(iter, b, iter->data);
		heap_sift_down(iter, 0, key_sort_cmp);
	}

	dst->u64s = cpu_to_le16((u64 *) out - dst->_data);
	return nr;
}

/* Common among btree and extent ptrs */

const struct bch_extent_ptr *
bch2_extent_has_device(struct bkey_s_c_extent e, unsigned dev)
{
	const struct bch_extent_ptr *ptr;

	extent_for_each_ptr(e, ptr)
		if (ptr->dev == dev)
			return ptr;

	return NULL;
}

bool bch2_extent_drop_device(struct bkey_s_extent e, unsigned dev)
{
	struct bch_extent_ptr *ptr;
	bool dropped = false;

	extent_for_each_ptr_backwards(e, ptr)
		if (ptr->dev == dev) {
			__bch2_extent_drop_ptr(e, ptr);
			dropped = true;
		}

	if (dropped)
		bch2_extent_drop_redundant_crcs(e);
	return dropped;
}

const struct bch_extent_ptr *
bch2_extent_has_group(struct bch_fs *c, struct bkey_s_c_extent e, unsigned group)
{
	const struct bch_extent_ptr *ptr;

	extent_for_each_ptr(e, ptr) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);

		if (ca->mi.group &&
		    ca->mi.group - 1 == group)
			return ptr;
	}

	return NULL;
}

const struct bch_extent_ptr *
bch2_extent_has_target(struct bch_fs *c, struct bkey_s_c_extent e, unsigned target)
{
	const struct bch_extent_ptr *ptr;

	extent_for_each_ptr(e, ptr)
		if (bch2_dev_in_target(c, ptr->dev, target) &&
		    (!ptr->cached ||
		     !ptr_stale(bch_dev_bkey_exists(c, ptr->dev), ptr)))
			return ptr;

	return NULL;
}

unsigned bch2_extent_nr_ptrs(struct bkey_s_c_extent e)
{
	const struct bch_extent_ptr *ptr;
	unsigned nr_ptrs = 0;

	extent_for_each_ptr(e, ptr)
		nr_ptrs++;

	return nr_ptrs;
}

unsigned bch2_extent_nr_dirty_ptrs(struct bkey_s_c k)
{
	struct bkey_s_c_extent e;
	const struct bch_extent_ptr *ptr;
	unsigned nr_ptrs = 0;

	switch (k.k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		e = bkey_s_c_to_extent(k);

		extent_for_each_ptr(e, ptr)
			nr_ptrs += !ptr->cached;
		break;

	case BCH_RESERVATION:
		nr_ptrs = bkey_s_c_to_reservation(k).v->nr_replicas;
		break;
	}

	return nr_ptrs;
}

unsigned bch2_extent_ptr_durability(struct bch_fs *c,
				    const struct bch_extent_ptr *ptr)
{
	struct bch_dev *ca;

	if (ptr->cached)
		return 0;

	ca = bch_dev_bkey_exists(c, ptr->dev);

	if (ca->mi.state == BCH_MEMBER_STATE_FAILED)
		return 0;

	return ca->mi.durability;
}

unsigned bch2_extent_durability(struct bch_fs *c, struct bkey_s_c_extent e)
{
	const struct bch_extent_ptr *ptr;
	unsigned durability = 0;

	extent_for_each_ptr(e, ptr)
		durability += bch2_extent_ptr_durability(c, ptr);

	return durability;
}

unsigned bch2_extent_is_compressed(struct bkey_s_c k)
{
	struct bkey_s_c_extent e;
	const struct bch_extent_ptr *ptr;
	struct bch_extent_crc_unpacked crc;
	unsigned ret = 0;

	switch (k.k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		e = bkey_s_c_to_extent(k);

		extent_for_each_ptr_crc(e, ptr, crc)
			if (!ptr->cached &&
			    crc.compression_type != BCH_COMPRESSION_NONE &&
			    crc.compressed_size < crc.live_size)
				ret = max_t(unsigned, ret, crc.compressed_size);
	}

	return ret;
}

bool bch2_extent_matches_ptr(struct bch_fs *c, struct bkey_s_c_extent e,
			     struct bch_extent_ptr m, u64 offset)
{
	const struct bch_extent_ptr *ptr;
	struct bch_extent_crc_unpacked crc;

	extent_for_each_ptr_crc(e, ptr, crc)
		if (ptr->dev	== m.dev &&
		    ptr->gen	== m.gen &&
		    (s64) ptr->offset + crc.offset - bkey_start_offset(e.k) ==
		    (s64) m.offset  - offset)
			return ptr;

	return NULL;
}

/* Doesn't cleanup redundant crcs */
void __bch2_extent_drop_ptr(struct bkey_s_extent e, struct bch_extent_ptr *ptr)
{
	EBUG_ON(ptr < &e.v->start->ptr ||
		ptr >= &extent_entry_last(e)->ptr);
	EBUG_ON(ptr->type != 1 << BCH_EXTENT_ENTRY_ptr);
	memmove_u64s_down(ptr, ptr + 1,
			  (u64 *) extent_entry_last(e) - (u64 *) (ptr + 1));
	e.k->u64s -= sizeof(*ptr) / sizeof(u64);
}

void bch2_extent_drop_ptr(struct bkey_s_extent e, struct bch_extent_ptr *ptr)
{
	__bch2_extent_drop_ptr(e, ptr);
	bch2_extent_drop_redundant_crcs(e);
}

static inline bool can_narrow_crc(struct bch_extent_crc_unpacked u,
				  struct bch_extent_crc_unpacked n)
{
	return !u.compression_type &&
		u.csum_type &&
		u.uncompressed_size > u.live_size &&
		bch2_csum_type_is_encryption(u.csum_type) ==
		bch2_csum_type_is_encryption(n.csum_type);
}

bool bch2_can_narrow_extent_crcs(struct bkey_s_c_extent e,
				 struct bch_extent_crc_unpacked n)
{
	struct bch_extent_crc_unpacked crc;
	const union bch_extent_entry *i;

	if (!n.csum_type)
		return false;

	extent_for_each_crc(e, crc, i)
		if (can_narrow_crc(crc, n))
			return true;

	return false;
}

/*
 * We're writing another replica for this extent, so while we've got the data in
 * memory we'll be computing a new checksum for the currently live data.
 *
 * If there are other replicas we aren't moving, and they are checksummed but
 * not compressed, we can modify them to point to only the data that is
 * currently live (so that readers won't have to bounce) while we've got the
 * checksum we need:
 */
bool bch2_extent_narrow_crcs(struct bkey_i_extent *e,
			     struct bch_extent_crc_unpacked n)
{
	struct bch_extent_crc_unpacked u;
	struct bch_extent_ptr *ptr;
	union bch_extent_entry *i;

	/* Find a checksum entry that covers only live data: */
	if (!n.csum_type)
		extent_for_each_crc(extent_i_to_s(e), u, i)
			if (!u.compression_type &&
			    u.csum_type &&
			    u.live_size == u.uncompressed_size) {
				n = u;
				break;
			}

	if (!bch2_can_narrow_extent_crcs(extent_i_to_s_c(e), n))
		return false;

	BUG_ON(n.compression_type);
	BUG_ON(n.offset);
	BUG_ON(n.live_size != e->k.size);

	bch2_extent_crc_append(e, n);
restart_narrow_pointers:
	extent_for_each_ptr_crc(extent_i_to_s(e), ptr, u)
		if (can_narrow_crc(u, n)) {
			ptr->offset += u.offset;
			extent_ptr_append(e, *ptr);
			__bch2_extent_drop_ptr(extent_i_to_s(e), ptr);
			goto restart_narrow_pointers;
		}

	bch2_extent_drop_redundant_crcs(extent_i_to_s(e));
	return true;
}

/* returns true if not equal */
static inline bool bch2_crc_unpacked_cmp(struct bch_extent_crc_unpacked l,
					 struct bch_extent_crc_unpacked r)
{
	return (l.csum_type		!= r.csum_type ||
		l.compression_type	!= r.compression_type ||
		l.compressed_size	!= r.compressed_size ||
		l.uncompressed_size	!= r.uncompressed_size ||
		l.offset		!= r.offset ||
		l.live_size		!= r.live_size ||
		l.nonce			!= r.nonce ||
		bch2_crc_cmp(l.csum, r.csum));
}

void bch2_extent_drop_redundant_crcs(struct bkey_s_extent e)
{
	union bch_extent_entry *entry = e.v->start;
	union bch_extent_crc *crc, *prev = NULL;
	struct bch_extent_crc_unpacked u, prev_u = { 0 };

	while (entry != extent_entry_last(e)) {
		union bch_extent_entry *next = extent_entry_next(entry);
		size_t crc_u64s = extent_entry_u64s(entry);

		if (!extent_entry_is_crc(entry))
			goto next;

		crc = entry_to_crc(entry);
		u = bch2_extent_crc_unpack(e.k, crc);

		if (next == extent_entry_last(e)) {
			/* crc entry with no pointers after it: */
			goto drop;
		}

		if (extent_entry_is_crc(next)) {
			/* no pointers before next crc entry: */
			goto drop;
		}

		if (prev && !bch2_crc_unpacked_cmp(u, prev_u)) {
			/* identical to previous crc entry: */
			goto drop;
		}

		if (!prev &&
		    !u.csum_type &&
		    !u.compression_type) {
			/* null crc entry: */
			union bch_extent_entry *e2;

			extent_for_each_entry_from(e, e2, extent_entry_next(entry)) {
				if (!extent_entry_is_ptr(e2))
					break;

				e2->ptr.offset += u.offset;
			}
			goto drop;
		}

		prev = crc;
		prev_u = u;
next:
		entry = next;
		continue;
drop:
		memmove_u64s_down(crc, next,
				  (u64 *) extent_entry_last(e) - (u64 *) next);
		e.k->u64s -= crc_u64s;
	}

	EBUG_ON(bkey_val_u64s(e.k) && !bch2_extent_nr_ptrs(e.c));
}

static bool should_drop_ptr(const struct bch_fs *c,
			    struct bkey_s_c_extent e,
			    const struct bch_extent_ptr *ptr)
{
	return ptr->cached && ptr_stale(bch_dev_bkey_exists(c, ptr->dev), ptr);
}

static void bch2_extent_drop_stale(struct bch_fs *c, struct bkey_s_extent e)
{
	struct bch_extent_ptr *ptr = &e.v->start->ptr;
	bool dropped = false;

	while ((ptr = extent_ptr_next(e, ptr)))
		if (should_drop_ptr(c, e.c, ptr)) {
			__bch2_extent_drop_ptr(e, ptr);
			dropped = true;
		} else
			ptr++;

	if (dropped)
		bch2_extent_drop_redundant_crcs(e);
}

bool bch2_ptr_normalize(struct bch_fs *c, struct btree *b, struct bkey_s k)
{
	return bch2_extent_normalize(c, k);
}

void bch2_ptr_swab(const struct bkey_format *f, struct bkey_packed *k)
{
	switch (k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED: {
		union bch_extent_entry *entry;
		u64 *d = (u64 *) bkeyp_val(f, k);
		unsigned i;

		for (i = 0; i < bkeyp_val_u64s(f, k); i++)
			d[i] = swab64(d[i]);

		for (entry = (union bch_extent_entry *) d;
		     entry < (union bch_extent_entry *) (d + bkeyp_val_u64s(f, k));
		     entry = extent_entry_next(entry)) {
			switch (extent_entry_type(entry)) {
			case BCH_EXTENT_ENTRY_crc32:
				entry->crc32.csum = swab32(entry->crc32.csum);
				break;
			case BCH_EXTENT_ENTRY_crc64:
				entry->crc64.csum_hi = swab16(entry->crc64.csum_hi);
				entry->crc64.csum_lo = swab64(entry->crc64.csum_lo);
				break;
			case BCH_EXTENT_ENTRY_crc128:
				entry->crc128.csum.hi = (__force __le64)
					swab64((__force u64) entry->crc128.csum.hi);
				entry->crc128.csum.lo = (__force __le64)
					swab64((__force u64) entry->crc128.csum.lo);
				break;
			case BCH_EXTENT_ENTRY_ptr:
				break;
			}
		}
		break;
	}
	}
}

static const char *extent_ptr_invalid(const struct bch_fs *c,
				      struct bkey_s_c_extent e,
				      const struct bch_extent_ptr *ptr,
				      unsigned size_ondisk,
				      bool metadata)
{
	const struct bch_extent_ptr *ptr2;
	struct bch_dev *ca;

	if (ptr->dev >= c->sb.nr_devices ||
	    !c->devs[ptr->dev])
		return "pointer to invalid device";

	ca = bch_dev_bkey_exists(c, ptr->dev);
	if (!ca)
		return "pointer to invalid device";

	extent_for_each_ptr(e, ptr2)
		if (ptr != ptr2 && ptr->dev == ptr2->dev)
			return "multiple pointers to same device";

	if (ptr->offset + size_ondisk > bucket_to_sector(ca, ca->mi.nbuckets))
		return "offset past end of device";

	if (ptr->offset < bucket_to_sector(ca, ca->mi.first_bucket))
		return "offset before first bucket";

	if (bucket_remainder(ca, ptr->offset) +
	    size_ondisk > ca->mi.bucket_size)
		return "spans multiple buckets";

	return NULL;
}

static size_t extent_print_ptrs(struct bch_fs *c, char *buf,
				size_t size, struct bkey_s_c_extent e)
{
	char *out = buf, *end = buf + size;
	const union bch_extent_entry *entry;
	struct bch_extent_crc_unpacked crc;
	const struct bch_extent_ptr *ptr;
	struct bch_dev *ca;
	bool first = true;

#define p(...)	(out += scnprintf(out, end - out, __VA_ARGS__))

	extent_for_each_entry(e, entry) {
		if (!first)
			p(" ");

		switch (__extent_entry_type(entry)) {
		case BCH_EXTENT_ENTRY_crc32:
		case BCH_EXTENT_ENTRY_crc64:
		case BCH_EXTENT_ENTRY_crc128:
			crc = bch2_extent_crc_unpack(e.k, entry_to_crc(entry));

			p("crc: c_size %u size %u offset %u nonce %u csum %u compress %u",
			  crc.compressed_size,
			  crc.uncompressed_size,
			  crc.offset, crc.nonce,
			  crc.csum_type,
			  crc.compression_type);
			break;
		case BCH_EXTENT_ENTRY_ptr:
			ptr = entry_to_ptr(entry);
			ca = ptr->dev < c->sb.nr_devices && c->devs[ptr->dev]
				? bch_dev_bkey_exists(c, ptr->dev)
				: NULL;

			p("ptr: %u:%llu gen %u%s%s", ptr->dev,
			  (u64) ptr->offset, ptr->gen,
			  ptr->cached ? " cached" : "",
			  ca && ptr_stale(ca, ptr)
			  ? " stale" : "");
			break;
		default:
			p("(invalid extent entry %.16llx)", *((u64 *) entry));
			goto out;
		}

		first = false;
	}
out:
	if (bkey_extent_is_cached(e.k))
		p(" cached");
#undef p
	return out - buf;
}

static inline bool dev_latency_better(struct bch_fs *c,
			      const struct bch_extent_ptr *ptr1,
			      const struct bch_extent_ptr *ptr2)
{
	struct bch_dev *dev1 = bch_dev_bkey_exists(c, ptr1->dev);
	struct bch_dev *dev2 = bch_dev_bkey_exists(c, ptr2->dev);
	u64 l1 = atomic64_read(&dev1->cur_latency[READ]);
	u64 l2 = atomic64_read(&dev2->cur_latency[READ]);

	/* Pick at random, biased in favor of the faster device: */

	return bch2_rand_range(l1 + l2) > l1;
}

static int extent_pick_read_device(struct bch_fs *c,
				   struct bkey_s_c_extent e,
				   struct bch_devs_mask *avoid,
				   struct extent_pick_ptr *pick)
{
	const struct bch_extent_ptr *ptr;
	struct bch_extent_crc_unpacked crc;
	struct bch_dev *ca;
	int ret = 0;

	extent_for_each_ptr_crc(e, ptr, crc) {
		ca = bch_dev_bkey_exists(c, ptr->dev);

		if (ptr->cached && ptr_stale(ca, ptr))
			continue;

		if (avoid && test_bit(ptr->dev, avoid->d))
			continue;

		if (ret && !dev_latency_better(c, ptr, &pick->ptr))
			continue;

		*pick = (struct extent_pick_ptr) {
			.ptr	= *ptr,
			.crc	= crc,
		};

		ret = 1;
	}

	return ret;
}

/* Btree ptrs */

const char *bch2_btree_ptr_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	if (bkey_extent_is_cached(k.k))
		return "cached";

	if (k.k->size)
		return "nonzero key size";

	if (bkey_val_u64s(k.k) > BKEY_BTREE_PTR_VAL_U64s_MAX)
		return "value too big";

	switch (k.k->type) {
	case BCH_EXTENT: {
		struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
		const union bch_extent_entry *entry;
		const struct bch_extent_ptr *ptr;
		const char *reason;

		extent_for_each_entry(e, entry) {
			if (__extent_entry_type(entry) >= BCH_EXTENT_ENTRY_MAX)
				return "invalid extent entry type";

			if (extent_entry_is_crc(entry))
				return "has crc field";
		}

		extent_for_each_ptr(e, ptr) {
			reason = extent_ptr_invalid(c, e, ptr,
						    c->opts.btree_node_size,
						    true);
			if (reason)
				return reason;
		}

		return NULL;
	}

	default:
		return "invalid value type";
	}
}

void bch2_btree_ptr_debugcheck(struct bch_fs *c, struct btree *b,
			       struct bkey_s_c k)
{
	struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
	const struct bch_extent_ptr *ptr;
	unsigned seq;
	const char *err;
	char buf[160];
	struct bucket_mark mark;
	struct bch_dev *ca;
	unsigned replicas = 0;
	bool bad;

	extent_for_each_ptr(e, ptr) {
		ca = bch_dev_bkey_exists(c, ptr->dev);
		replicas++;

		if (!test_bit(BCH_FS_ALLOC_READ_DONE, &c->flags))
			continue;

		err = "stale";
		if (ptr_stale(ca, ptr))
			goto err;

		do {
			seq = read_seqcount_begin(&c->gc_pos_lock);
			mark = ptr_bucket_mark(ca, ptr);

			bad = gc_pos_cmp(c->gc_pos, gc_pos_btree_node(b)) > 0 &&
				(mark.data_type != BCH_DATA_BTREE ||
				 mark.dirty_sectors < c->opts.btree_node_size);
		} while (read_seqcount_retry(&c->gc_pos_lock, seq));

		err = "inconsistent";
		if (bad)
			goto err;
	}

	if (!bch2_bkey_replicas_marked(c, BCH_DATA_BTREE, e.s_c)) {
		bch2_bkey_val_to_text(c, btree_node_type(b),
				     buf, sizeof(buf), k);
		bch2_fs_bug(c,
			"btree key bad (replicas not marked in superblock):\n%s",
			buf);
		return;
	}

	return;
err:
	bch2_bkey_val_to_text(c, btree_node_type(b), buf, sizeof(buf), k);
	bch2_fs_bug(c, "%s btree pointer %s: bucket %zi "
		      "gen %i mark %08x",
		      err, buf, PTR_BUCKET_NR(ca, ptr),
		      mark.gen, (unsigned) mark.v.counter);
}

int bch2_btree_ptr_to_text(struct bch_fs *c, char *buf,
			   size_t size, struct bkey_s_c k)
{
	char *out = buf, *end = buf + size;
	const char *invalid;

#define p(...)	(out += scnprintf(out, end - out, __VA_ARGS__))

	if (bkey_extent_is_data(k.k))
		out += extent_print_ptrs(c, buf, size, bkey_s_c_to_extent(k));

	invalid = bch2_btree_ptr_invalid(c, k);
	if (invalid)
		p(" invalid: %s", invalid);
#undef p
	return out - buf;
}

int bch2_btree_pick_ptr(struct bch_fs *c, const struct btree *b,
			struct bch_devs_mask *avoid,
			struct extent_pick_ptr *pick)
{
	return extent_pick_read_device(c, bkey_i_to_s_c_extent(&b->key),
				       avoid, pick);
}

/* Extents */

static bool __bch2_cut_front(struct bpos where, struct bkey_s k)
{
	u64 len = 0;

	if (bkey_cmp(where, bkey_start_pos(k.k)) <= 0)
		return false;

	EBUG_ON(bkey_cmp(where, k.k->p) > 0);

	len = k.k->p.offset - where.offset;

	BUG_ON(len > k.k->size);

	/*
	 * Don't readjust offset if the key size is now 0, because that could
	 * cause offset to point to the next bucket:
	 */
	if (!len)
		k.k->type = KEY_TYPE_DELETED;
	else if (bkey_extent_is_data(k.k)) {
		struct bkey_s_extent e = bkey_s_to_extent(k);
		union bch_extent_entry *entry;
		bool seen_crc = false;

		extent_for_each_entry(e, entry) {
			switch (extent_entry_type(entry)) {
			case BCH_EXTENT_ENTRY_ptr:
				if (!seen_crc)
					entry->ptr.offset += e.k->size - len;
				break;
			case BCH_EXTENT_ENTRY_crc32:
				entry->crc32.offset += e.k->size - len;
				break;
			case BCH_EXTENT_ENTRY_crc64:
				entry->crc64.offset += e.k->size - len;
				break;
			case BCH_EXTENT_ENTRY_crc128:
				entry->crc128.offset += e.k->size - len;
				break;
			}

			if (extent_entry_is_crc(entry))
				seen_crc = true;
		}
	}

	k.k->size = len;

	return true;
}

bool bch2_cut_front(struct bpos where, struct bkey_i *k)
{
	return __bch2_cut_front(where, bkey_i_to_s(k));
}

bool bch2_cut_back(struct bpos where, struct bkey *k)
{
	u64 len = 0;

	if (bkey_cmp(where, k->p) >= 0)
		return false;

	EBUG_ON(bkey_cmp(where, bkey_start_pos(k)) < 0);

	len = where.offset - bkey_start_offset(k);

	BUG_ON(len > k->size);

	k->p = where;
	k->size = len;

	if (!len)
		k->type = KEY_TYPE_DELETED;

	return true;
}

/**
 * bch_key_resize - adjust size of @k
 *
 * bkey_start_offset(k) will be preserved, modifies where the extent ends
 */
void bch2_key_resize(struct bkey *k,
		    unsigned new_size)
{
	k->p.offset -= k->size;
	k->p.offset += new_size;
	k->size = new_size;
}

/*
 * In extent_sort_fix_overlapping(), insert_fixup_extent(),
 * extent_merge_inline() - we're modifying keys in place that are packed. To do
 * that we have to unpack the key, modify the unpacked key - then this
 * copies/repacks the unpacked to the original as necessary.
 */
static void extent_save(struct btree *b, struct bkey_packed *dst,
			struct bkey *src)
{
	struct bkey_format *f = &b->format;
	struct bkey_i *dst_unpacked;

	if ((dst_unpacked = packed_to_bkey(dst)))
		dst_unpacked->k = *src;
	else
		BUG_ON(!bch2_bkey_pack_key(dst, src, f));
}

static bool extent_i_save(struct btree *b, struct bkey_packed *dst,
			  struct bkey_i *src)
{
	struct bkey_format *f = &b->format;
	struct bkey_i *dst_unpacked;
	struct bkey_packed tmp;

	if ((dst_unpacked = packed_to_bkey(dst)))
		dst_unpacked->k = src->k;
	else if (bch2_bkey_pack_key(&tmp, &src->k, f))
		memcpy_u64s(dst, &tmp, f->key_u64s);
	else
		return false;

	memcpy_u64s(bkeyp_val(f, dst), &src->v, bkey_val_u64s(&src->k));
	return true;
}

/*
 * If keys compare equal, compare by pointer order:
 *
 * Necessary for sort_fix_overlapping() - if there are multiple keys that
 * compare equal in different sets, we have to process them newest to oldest.
 */
#define extent_sort_cmp(h, l, r)					\
({									\
	struct bkey _ul = bkey_unpack_key(b,				\
				__btree_node_offset_to_key(b, (l).k));	\
	struct bkey _ur = bkey_unpack_key(b,				\
				__btree_node_offset_to_key(b, (r).k));	\
									\
	bkey_cmp(bkey_start_pos(&_ul),					\
		 bkey_start_pos(&_ur)) ?: (r).k - (l).k;		\
})

static inline void extent_sort_sift(struct btree_node_iter_large *iter,
				    struct btree *b, size_t i)
{
	heap_sift_down(iter, i, extent_sort_cmp);
}

static inline void extent_sort_next(struct btree_node_iter_large *iter,
				    struct btree *b,
				    struct btree_node_iter_set *i)
{
	sort_key_next(iter, b, i);
	heap_sift_down(iter, i - iter->data, extent_sort_cmp);
}

static void extent_sort_append(struct bch_fs *c,
			       struct btree *b,
			       struct btree_nr_keys *nr,
			       struct bkey_packed *start,
			       struct bkey_packed **prev,
			       struct bkey_packed *k)
{
	struct bkey_format *f = &b->format;
	BKEY_PADDED(k) tmp;

	if (bkey_whiteout(k))
		return;

	bch2_bkey_unpack(b, &tmp.k, k);

	if (*prev &&
	    bch2_extent_merge(c, b, (void *) *prev, &tmp.k))
		return;

	if (*prev) {
		bch2_bkey_pack(*prev, (void *) *prev, f);

		btree_keys_account_key_add(nr, 0, *prev);
		*prev = bkey_next(*prev);
	} else {
		*prev = start;
	}

	bkey_copy(*prev, &tmp.k);
}

struct btree_nr_keys bch2_extent_sort_fix_overlapping(struct bch_fs *c,
					struct bset *dst,
					struct btree *b,
					struct btree_node_iter_large *iter)
{
	struct bkey_format *f = &b->format;
	struct btree_node_iter_set *_l = iter->data, *_r;
	struct bkey_packed *prev = NULL, *out, *lk, *rk;
	struct bkey l_unpacked, r_unpacked;
	struct bkey_s l, r;
	struct btree_nr_keys nr;

	memset(&nr, 0, sizeof(nr));

	heap_resort(iter, extent_sort_cmp);

	while (!bch2_btree_node_iter_large_end(iter)) {
		lk = __btree_node_offset_to_key(b, _l->k);

		if (iter->used == 1) {
			extent_sort_append(c, b, &nr, dst->start, &prev, lk);
			extent_sort_next(iter, b, _l);
			continue;
		}

		_r = iter->data + 1;
		if (iter->used > 2 &&
		    extent_sort_cmp(iter, _r[0], _r[1]) >= 0)
			_r++;

		rk = __btree_node_offset_to_key(b, _r->k);

		l = __bkey_disassemble(b, lk, &l_unpacked);
		r = __bkey_disassemble(b, rk, &r_unpacked);

		/* If current key and next key don't overlap, just append */
		if (bkey_cmp(l.k->p, bkey_start_pos(r.k)) <= 0) {
			extent_sort_append(c, b, &nr, dst->start, &prev, lk);
			extent_sort_next(iter, b, _l);
			continue;
		}

		/* Skip 0 size keys */
		if (!r.k->size) {
			extent_sort_next(iter, b, _r);
			continue;
		}

		/*
		 * overlap: keep the newer key and trim the older key so they
		 * don't overlap. comparing pointers tells us which one is
		 * newer, since the bsets are appended one after the other.
		 */

		/* can't happen because of comparison func */
		BUG_ON(_l->k < _r->k &&
		       !bkey_cmp(bkey_start_pos(l.k), bkey_start_pos(r.k)));

		if (_l->k > _r->k) {
			/* l wins, trim r */
			if (bkey_cmp(l.k->p, r.k->p) >= 0) {
				sort_key_next(iter, b, _r);
			} else {
				__bch2_cut_front(l.k->p, r);
				extent_save(b, rk, r.k);
			}

			extent_sort_sift(iter, b, _r - iter->data);
		} else if (bkey_cmp(l.k->p, r.k->p) > 0) {
			BKEY_PADDED(k) tmp;

			/*
			 * r wins, but it overlaps in the middle of l - split l:
			 */
			bkey_reassemble(&tmp.k, l.s_c);
			bch2_cut_back(bkey_start_pos(r.k), &tmp.k.k);

			__bch2_cut_front(r.k->p, l);
			extent_save(b, lk, l.k);

			extent_sort_sift(iter, b, 0);

			extent_sort_append(c, b, &nr, dst->start, &prev,
					   bkey_to_packed(&tmp.k));
		} else {
			bch2_cut_back(bkey_start_pos(r.k), l.k);
			extent_save(b, lk, l.k);
		}
	}

	if (prev) {
		bch2_bkey_pack(prev, (void *) prev, f);
		btree_keys_account_key_add(&nr, 0, prev);
		out = bkey_next(prev);
	} else {
		out = dst->start;
	}

	dst->u64s = cpu_to_le16((u64 *) out - dst->_data);
	return nr;
}

struct extent_insert_state {
	struct btree_insert		*trans;
	struct btree_insert_entry	*insert;
	struct bpos			committed;
	struct bch_fs_usage		stats;

	/* for deleting: */
	struct bkey_i			whiteout;
	bool				do_journal;
	bool				deleting;
};

static void bch2_add_sectors(struct extent_insert_state *s,
			     struct bkey_s_c k, u64 offset, s64 sectors)
{
	struct bch_fs *c = s->trans->c;
	struct btree *b = s->insert->iter->l[0].b;

	EBUG_ON(bkey_cmp(bkey_start_pos(k.k), b->data->min_key) < 0);

	if (!sectors)
		return;

	bch2_mark_key(c, k, sectors, BCH_DATA_USER, gc_pos_btree_node(b),
		      &s->stats, s->trans->journal_res.seq, 0);
}

static void bch2_subtract_sectors(struct extent_insert_state *s,
				 struct bkey_s_c k, u64 offset, s64 sectors)
{
	bch2_add_sectors(s, k, offset, -sectors);
}

/* These wrappers subtract exactly the sectors that we're removing from @k */
static void bch2_cut_subtract_back(struct extent_insert_state *s,
				  struct bpos where, struct bkey_s k)
{
	bch2_subtract_sectors(s, k.s_c, where.offset,
			     k.k->p.offset - where.offset);
	bch2_cut_back(where, k.k);
}

static void bch2_cut_subtract_front(struct extent_insert_state *s,
				   struct bpos where, struct bkey_s k)
{
	bch2_subtract_sectors(s, k.s_c, bkey_start_offset(k.k),
			     where.offset - bkey_start_offset(k.k));
	__bch2_cut_front(where, k);
}

static void bch2_drop_subtract(struct extent_insert_state *s, struct bkey_s k)
{
	if (k.k->size)
		bch2_subtract_sectors(s, k.s_c,
				     bkey_start_offset(k.k), k.k->size);
	k.k->size = 0;
	k.k->type = KEY_TYPE_DELETED;
}

static bool bch2_extent_merge_inline(struct bch_fs *,
				     struct btree_iter *,
				     struct bkey_packed *,
				     struct bkey_packed *,
				     bool);

static enum btree_insert_ret
extent_insert_should_stop(struct extent_insert_state *s)
{
	struct btree *b = s->insert->iter->l[0].b;

	/*
	 * Check if we have sufficient space in both the btree node and the
	 * journal reservation:
	 *
	 * Each insert checks for room in the journal entry, but we check for
	 * room in the btree node up-front. In the worst case, bkey_cmpxchg()
	 * will insert two keys, and one iteration of this room will insert one
	 * key, so we need room for three keys.
	 */
	if (!bch2_btree_node_insert_fits(s->trans->c, b, s->insert->k->k.u64s))
		return BTREE_INSERT_BTREE_NODE_FULL;
	else if (!journal_res_insert_fits(s->trans, s->insert))
		return BTREE_INSERT_JOURNAL_RES_FULL; /* XXX worth tracing */
	else
		return BTREE_INSERT_OK;
}

static void verify_extent_nonoverlapping(struct btree *b,
					 struct btree_node_iter *_iter,
					 struct bkey_i *insert)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct btree_node_iter iter;
	struct bkey_packed *k;
	struct bkey uk;

	iter = *_iter;
	k = bch2_btree_node_iter_prev_filter(&iter, b, KEY_TYPE_DISCARD);
	BUG_ON(k &&
	       (uk = bkey_unpack_key(b, k),
		bkey_cmp(uk.p, bkey_start_pos(&insert->k)) > 0));

	iter = *_iter;
	k = bch2_btree_node_iter_peek_filter(&iter, b, KEY_TYPE_DISCARD);
#if 0
	BUG_ON(k &&
	       (uk = bkey_unpack_key(b, k),
		bkey_cmp(insert->k.p, bkey_start_pos(&uk))) > 0);
#else
	if (k &&
	    (uk = bkey_unpack_key(b, k),
	     bkey_cmp(insert->k.p, bkey_start_pos(&uk))) > 0) {
		char buf1[100];
		char buf2[100];

		bch2_bkey_to_text(buf1, sizeof(buf1), &insert->k);
		bch2_bkey_to_text(buf2, sizeof(buf2), &uk);

		bch2_dump_btree_node(b);
		panic("insert > next :\n"
		      "insert %s\n"
		      "next   %s\n",
		      buf1, buf2);
	}
#endif

#endif
}

static void verify_modified_extent(struct btree_iter *iter,
				   struct bkey_packed *k)
{
	bch2_btree_iter_verify(iter, iter->l[0].b);
	bch2_verify_insert_pos(iter->l[0].b, k, k, k->u64s);
}

static void extent_bset_insert(struct bch_fs *c, struct btree_iter *iter,
			       struct bkey_i *insert)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bset_tree *t = bset_tree_last(l->b);
	struct bkey_packed *where =
		bch2_btree_node_iter_bset_pos(&l->iter, l->b, t);
	struct bkey_packed *prev = bch2_bkey_prev_filter(l->b, t, where,
							 KEY_TYPE_DISCARD);
	struct bkey_packed *next_live_key = where;
	unsigned clobber_u64s;

	EBUG_ON(bkey_deleted(&insert->k) || !insert->k.size);
	verify_extent_nonoverlapping(l->b, &l->iter, insert);

	if (!prev) {
		while ((prev = bch2_bkey_prev_all(l->b, t, where)) &&
		       (bkey_cmp_left_packed(l->b, prev, &insert->k.p) ?:
			((int) bkey_deleted(&insert->k) - (int) bkey_deleted(prev))) > 0)
			where = prev;
	}

	if (prev)
		where = bkey_next(prev);

	while (next_live_key != btree_bkey_last(l->b, t) &&
	       bkey_deleted(next_live_key))
		next_live_key = bkey_next(next_live_key);

	/*
	 * Everything between where and next_live_key is now deleted keys, and
	 * is overwritten:
	 */
	clobber_u64s = (u64 *) next_live_key - (u64 *) where;

	if (prev &&
	    bch2_extent_merge_inline(c, iter, prev, bkey_to_packed(insert), true))
		goto drop_deleted_keys;

	if (next_live_key != btree_bkey_last(l->b, t) &&
	    bch2_extent_merge_inline(c, iter, bkey_to_packed(insert),
				    next_live_key, false))
		goto drop_deleted_keys;

	bch2_bset_insert(l->b, &l->iter, where, insert, clobber_u64s);
	bch2_btree_node_iter_fix(iter, l->b, &l->iter, t, where,
				 clobber_u64s, where->u64s);
	bch2_verify_key_order(l->b, &l->iter, where);
	bch2_btree_iter_verify(iter, l->b);
	return;
drop_deleted_keys:
	bch2_bset_delete(l->b, where, clobber_u64s);
	bch2_btree_node_iter_fix(iter, l->b, &l->iter, t,
				 where, clobber_u64s, 0);
	bch2_btree_iter_verify(iter, l->b);
}

static void extent_insert_committed(struct extent_insert_state *s)
{
	struct bch_fs *c = s->trans->c;
	struct btree_iter *iter = s->insert->iter;
	struct bkey_i *insert = !s->deleting
		? s->insert->k
		: &s->whiteout;
	BKEY_PADDED(k) split;

	EBUG_ON(bkey_deleted(&insert->k) || !insert->k.size);
	EBUG_ON(bkey_cmp(insert->k.p, s->committed) < 0);
	EBUG_ON(bkey_cmp(s->committed, bkey_start_pos(&insert->k)) < 0);

	if (!bkey_cmp(s->committed, bkey_start_pos(&insert->k)))
		return;

	if (s->deleting && !s->do_journal) {
		bch2_cut_front(s->committed, insert);
		goto done;
	}

	EBUG_ON(bkey_deleted(&insert->k) || !insert->k.size);

	bkey_copy(&split.k, insert);

	if (!(s->trans->flags & BTREE_INSERT_JOURNAL_REPLAY) &&
	    bkey_cmp(s->committed, insert->k.p) &&
	    bch2_extent_is_compressed(bkey_i_to_s_c(insert))) {
		/* XXX: possibly need to increase our reservation? */
		bch2_cut_subtract_back(s, s->committed,
				      bkey_i_to_s(&split.k));
		bch2_cut_front(s->committed, insert);
		bch2_add_sectors(s, bkey_i_to_s_c(insert),
				bkey_start_offset(&insert->k),
				insert->k.size);
	} else {
		bch2_cut_back(s->committed, &split.k.k);
		bch2_cut_front(s->committed, insert);
	}

	if (debug_check_bkeys(c))
		bch2_bkey_debugcheck(c, iter->l[0].b, bkey_i_to_s_c(&split.k));

	bch2_btree_journal_key(s->trans, iter, &split.k);

	if (!s->deleting) {
		bch2_btree_iter_set_pos_same_leaf(iter, s->committed);
		extent_bset_insert(c, iter, &split.k);
	}
done:
	bch2_btree_iter_set_pos_same_leaf(iter, s->committed);

	insert->k.needs_whiteout	= false;
	s->do_journal			= false;
	s->trans->did_work		= true;
}

static enum btree_insert_ret
__extent_insert_advance_pos(struct extent_insert_state *s,
			    struct bpos next_pos,
			    struct bkey_s_c k)
{
	struct extent_insert_hook *hook = s->trans->hook;
	enum btree_insert_ret ret;

	if (hook)
		ret = hook->fn(hook, s->committed, next_pos, k, s->insert->k);
	else
		ret = BTREE_INSERT_OK;

	if (ret == BTREE_INSERT_OK)
		s->committed = next_pos;

	return ret;
}

/*
 * Update iter->pos, marking how much of @insert we've processed, and call hook
 * fn:
 */
static enum btree_insert_ret
extent_insert_advance_pos(struct extent_insert_state *s, struct bkey_s_c k)
{
	struct btree *b = s->insert->iter->l[0].b;
	struct bpos next_pos = bpos_min(s->insert->k->k.p,
					k.k ? k.k->p : b->key.k.p);
	enum btree_insert_ret ret;

	if (race_fault())
		return BTREE_INSERT_NEED_TRAVERSE;

	/* hole? */
	if (k.k && bkey_cmp(s->committed, bkey_start_pos(k.k)) < 0) {
		ret = __extent_insert_advance_pos(s, bkey_start_pos(k.k),
						    bkey_s_c_null);
		if (ret != BTREE_INSERT_OK)
			return ret;
	}

	/* avoid redundant calls to hook fn: */
	if (!bkey_cmp(s->committed, next_pos))
		return BTREE_INSERT_OK;

	return __extent_insert_advance_pos(s, next_pos, k);
}

enum btree_insert_ret
bch2_extent_can_insert(struct btree_insert *trans,
		       struct btree_insert_entry *insert,
		       unsigned *u64s)
{
	struct btree_iter_level *l = &insert->iter->l[0];
	struct btree_node_iter node_iter = l->iter;
	enum bch_extent_overlap overlap;
	struct bkey_packed *_k;
	struct bkey unpacked;
	struct bkey_s_c k;
	int sectors;

	_k = bch2_btree_node_iter_peek_filter(&node_iter, l->b,
					      KEY_TYPE_DISCARD);
	if (!_k)
		return BTREE_INSERT_OK;

	k = bkey_disassemble(l->b, _k, &unpacked);

	overlap = bch2_extent_overlap(&insert->k->k, k.k);

	/* account for having to split existing extent: */
	if (overlap == BCH_EXTENT_OVERLAP_MIDDLE)
		*u64s += _k->u64s;

	if (overlap == BCH_EXTENT_OVERLAP_MIDDLE &&
	    (sectors = bch2_extent_is_compressed(k))) {
		int flags = BCH_DISK_RESERVATION_BTREE_LOCKS_HELD;

		if (trans->flags & BTREE_INSERT_NOFAIL)
			flags |= BCH_DISK_RESERVATION_NOFAIL;

		switch (bch2_disk_reservation_add(trans->c,
				trans->disk_res,
				sectors * bch2_extent_nr_dirty_ptrs(k),
				flags)) {
		case 0:
			break;
		case -ENOSPC:
			return BTREE_INSERT_ENOSPC;
		case -EINTR:
			return BTREE_INSERT_NEED_GC_LOCK;
		default:
			BUG();
		}
	}

	return BTREE_INSERT_OK;
}

static void
extent_squash(struct extent_insert_state *s, struct bkey_i *insert,
	      struct bset_tree *t, struct bkey_packed *_k, struct bkey_s k,
	      enum bch_extent_overlap overlap)
{
	struct bch_fs *c = s->trans->c;
	struct btree_iter *iter = s->insert->iter;
	struct btree_iter_level *l = &iter->l[0];
	struct btree *b = l->b;

	switch (overlap) {
	case BCH_EXTENT_OVERLAP_FRONT:
		/* insert overlaps with start of k: */
		bch2_cut_subtract_front(s, insert->k.p, k);
		BUG_ON(bkey_deleted(k.k));
		extent_save(b, _k, k.k);
		bch2_verify_key_order(b, &l->iter, _k);
		break;

	case BCH_EXTENT_OVERLAP_BACK:
		/* insert overlaps with end of k: */
		bch2_cut_subtract_back(s, bkey_start_pos(&insert->k), k);
		BUG_ON(bkey_deleted(k.k));
		extent_save(b, _k, k.k);

		/*
		 * As the auxiliary tree is indexed by the end of the
		 * key and we've just changed the end, update the
		 * auxiliary tree.
		 */
		bch2_bset_fix_invalidated_key(b, t, _k);
		bch2_btree_node_iter_fix(iter, b, &l->iter, t,
					 _k, _k->u64s, _k->u64s);
		bch2_verify_key_order(b, &l->iter, _k);
		break;

	case BCH_EXTENT_OVERLAP_ALL: {
		/* The insert key completely covers k, invalidate k */
		if (!bkey_whiteout(k.k))
			btree_keys_account_key_drop(&b->nr,
						t - b->set, _k);

		bch2_drop_subtract(s, k);

		if (t == bset_tree_last(l->b)) {
			unsigned u64s = _k->u64s;

			bch2_bset_delete(l->b, _k, _k->u64s);
			bch2_btree_node_iter_fix(iter, b, &l->iter, t,
						 _k, u64s, 0);
			bch2_btree_iter_verify(iter, b);
		} else {
			extent_save(b, _k, k.k);
			bch2_btree_node_iter_fix(iter, b, &l->iter, t,
						 _k, _k->u64s, _k->u64s);
			bch2_verify_key_order(b, &l->iter, _k);
		}

		break;
	}
	case BCH_EXTENT_OVERLAP_MIDDLE: {
		BKEY_PADDED(k) split;
		/*
		 * The insert key falls 'in the middle' of k
		 * The insert key splits k in 3:
		 * - start only in k, preserve
		 * - middle common section, invalidate in k
		 * - end only in k, preserve
		 *
		 * We update the old key to preserve the start,
		 * insert will be the new common section,
		 * we manually insert the end that we are preserving.
		 *
		 * modify k _before_ doing the insert (which will move
		 * what k points to)
		 */
		bkey_reassemble(&split.k, k.s_c);
		split.k.k.needs_whiteout |= bkey_written(b, _k);

		bch2_cut_back(bkey_start_pos(&insert->k), &split.k.k);
		BUG_ON(bkey_deleted(&split.k.k));

		bch2_cut_subtract_front(s, insert->k.p, k);
		BUG_ON(bkey_deleted(k.k));
		extent_save(b, _k, k.k);
		bch2_verify_key_order(b, &l->iter, _k);

		bch2_add_sectors(s, bkey_i_to_s_c(&split.k),
				bkey_start_offset(&split.k.k),
				split.k.k.size);
		extent_bset_insert(c, iter, &split.k);
		break;
	}
	}
}

static enum btree_insert_ret
__bch2_insert_fixup_extent(struct extent_insert_state *s)
{
	struct bch_fs *c = s->trans->c;
	struct btree_iter *iter = s->insert->iter;
	struct btree_iter_level *l = &iter->l[0];
	struct btree *b = l->b;
	struct bkey_packed *_k;
	struct bkey unpacked;
	struct bkey_i *insert = s->insert->k;
	enum btree_insert_ret ret = BTREE_INSERT_OK;

	while (bkey_cmp(s->committed, insert->k.p) < 0 &&
	       (ret = extent_insert_should_stop(s)) == BTREE_INSERT_OK &&
	       (_k = bch2_btree_node_iter_peek_filter(&l->iter, b, KEY_TYPE_DISCARD))) {
		struct bset_tree *t = bch2_bkey_to_bset(b, _k);
		struct bkey_s k = __bkey_disassemble(b, _k, &unpacked);
		enum bch_extent_overlap overlap;

		EBUG_ON(bkey_cmp(iter->pos, bkey_start_pos(&insert->k)));
		EBUG_ON(bkey_cmp(iter->pos, k.k->p) >= 0);

		if (bkey_cmp(bkey_start_pos(k.k), insert->k.p) >= 0)
			break;

		ret = extent_insert_advance_pos(s, k.s_c);
		if (ret)
			break;

		overlap = bch2_extent_overlap(&insert->k, k.k);

		if (!s->deleting) {
			if (k.k->needs_whiteout || bkey_written(b, _k))
				insert->k.needs_whiteout = true;

			if (overlap == BCH_EXTENT_OVERLAP_ALL &&
			    bkey_whiteout(k.k) &&
			    k.k->needs_whiteout) {
				unreserve_whiteout(b, _k);
				_k->needs_whiteout = false;
			}

			extent_squash(s, insert, t, _k, k, overlap);
		} else {
			if (bkey_whiteout(k.k))
				goto next;

			s->do_journal = true;

			if (overlap == BCH_EXTENT_OVERLAP_ALL) {
				btree_keys_account_key_drop(&b->nr,
							t - b->set, _k);
				bch2_subtract_sectors(s, k.s_c,
						     bkey_start_offset(k.k), k.k->size);
				_k->type = KEY_TYPE_DISCARD;
				reserve_whiteout(b, _k);
			} else if (k.k->needs_whiteout ||
				   bkey_written(b, _k)) {
				struct bkey_i discard = *insert;

				discard.k.type = KEY_TYPE_DISCARD;

				switch (overlap) {
				case BCH_EXTENT_OVERLAP_FRONT:
					bch2_cut_front(bkey_start_pos(k.k), &discard);
					break;
				case BCH_EXTENT_OVERLAP_BACK:
					bch2_cut_back(k.k->p, &discard.k);
					break;
				default:
					break;
				}

				discard.k.needs_whiteout = true;

				extent_squash(s, insert, t, _k, k, overlap);

				extent_bset_insert(c, iter, &discard);
			} else {
				extent_squash(s, insert, t, _k, k, overlap);
			}
next:
			bch2_cut_front(s->committed, insert);
			bch2_btree_iter_set_pos_same_leaf(iter, s->committed);
		}

		if (overlap == BCH_EXTENT_OVERLAP_FRONT ||
		    overlap == BCH_EXTENT_OVERLAP_MIDDLE)
			break;
	}

	if (ret == BTREE_INSERT_OK &&
	    bkey_cmp(s->committed, insert->k.p) < 0)
		ret = extent_insert_advance_pos(s, bkey_s_c_null);

	/*
	 * may have skipped past some deleted extents greater than the insert
	 * key, before we got to a non deleted extent and knew we could bail out
	 * rewind the iterator a bit if necessary:
	 */
	{
		struct btree_node_iter node_iter = l->iter;
		struct bkey uk;

		while ((_k = bch2_btree_node_iter_prev_all(&node_iter, l->b)) &&
		       (uk = bkey_unpack_key(l->b, _k),
			bkey_cmp(uk.p, s->committed) > 0))
			l->iter = node_iter;
	}

	return ret;
}

/**
 * bch_extent_insert_fixup - insert a new extent and deal with overlaps
 *
 * this may result in not actually doing the insert, or inserting some subset
 * of the insert key. For cmpxchg operations this is where that logic lives.
 *
 * All subsets of @insert that need to be inserted are inserted using
 * bch2_btree_insert_and_journal(). If @b or @res fills up, this function
 * returns false, setting @iter->pos for the prefix of @insert that actually got
 * inserted.
 *
 * BSET INVARIANTS: this function is responsible for maintaining all the
 * invariants for bsets of extents in memory. things get really hairy with 0
 * size extents
 *
 * within one bset:
 *
 * bkey_start_pos(bkey_next(k)) >= k
 * or bkey_start_offset(bkey_next(k)) >= k->offset
 *
 * i.e. strict ordering, no overlapping extents.
 *
 * multiple bsets (i.e. full btree node):
 *
 * ∀ k, j
 *   k.size != 0 ∧ j.size != 0 →
 *     ¬ (k > bkey_start_pos(j) ∧ k < j)
 *
 * i.e. no two overlapping keys _of nonzero size_
 *
 * We can't realistically maintain this invariant for zero size keys because of
 * the key merging done in bch2_btree_insert_key() - for two mergeable keys k, j
 * there may be another 0 size key between them in another bset, and it will
 * thus overlap with the merged key.
 *
 * In addition, the end of iter->pos indicates how much has been processed.
 * If the end of iter->pos is not the same as the end of insert, then
 * key insertion needs to continue/be retried.
 */
enum btree_insert_ret
bch2_insert_fixup_extent(struct btree_insert *trans,
			 struct btree_insert_entry *insert)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter = insert->iter;
	struct btree_iter_level *l = &iter->l[0];
	struct btree *b = l->b;
	enum btree_insert_ret ret = BTREE_INSERT_OK;

	struct extent_insert_state s = {
		.trans		= trans,
		.insert		= insert,
		.committed	= insert->iter->pos,
		.deleting	= bkey_whiteout(&insert->k->k),
	};

	if (s.deleting) {
		s.whiteout = *insert->k;
		s.whiteout.k.type = KEY_TYPE_DISCARD;
	}

	EBUG_ON(iter->level);
	EBUG_ON(!insert->k->k.size);

	/*
	 * As we process overlapping extents, we advance @iter->pos both to
	 * signal to our caller (btree_insert_key()) how much of @insert->k has
	 * been inserted, and also to keep @iter->pos consistent with
	 * @insert->k and the node iterator that we're advancing:
	 */
	EBUG_ON(bkey_cmp(iter->pos, bkey_start_pos(&insert->k->k)));
	bch2_btree_iter_verify(iter, b);

	if (!s.deleting &&
	    !(trans->flags & BTREE_INSERT_JOURNAL_REPLAY))
		bch2_add_sectors(&s, bkey_i_to_s_c(insert->k),
				bkey_start_offset(&insert->k->k),
				insert->k->k.size);

	ret = __bch2_insert_fixup_extent(&s);

	extent_insert_committed(&s);

	if (s.deleting)
		bch2_cut_front(iter->pos, insert->k);

	/*
	 * Subtract any remaining sectors from @insert, if we bailed out early
	 * and didn't fully insert @insert:
	 */
	if (!s.deleting &&
	    !(trans->flags & BTREE_INSERT_JOURNAL_REPLAY) &&
	    insert->k->k.size)
		bch2_subtract_sectors(&s, bkey_i_to_s_c(insert->k),
				     bkey_start_offset(&insert->k->k),
				     insert->k->k.size);

	bch2_fs_usage_apply(c, &s.stats, trans->disk_res,
			   gc_pos_btree_node(b));

	EBUG_ON(bkey_cmp(iter->pos, bkey_start_pos(&insert->k->k)));
	EBUG_ON(bkey_cmp(iter->pos, s.committed));
	EBUG_ON((bkey_cmp(iter->pos, b->key.k.p) == 0) !=
		!!(iter->flags & BTREE_ITER_AT_END_OF_LEAF));

	if (insert->k->k.size && (iter->flags & BTREE_ITER_AT_END_OF_LEAF))
		ret = BTREE_INSERT_NEED_TRAVERSE;

	WARN_ONCE((ret == BTREE_INSERT_OK) != (insert->k->k.size == 0),
		  "ret %u insert->k.size %u", ret, insert->k->k.size);

	return ret;
}

const char *bch2_extent_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	if (bkey_val_u64s(k.k) > BKEY_EXTENT_VAL_U64s_MAX)
		return "value too big";

	if (!k.k->size)
		return "zero key size";

	switch (k.k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED: {
		struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
		const union bch_extent_entry *entry;
		struct bch_extent_crc_unpacked crc;
		const struct bch_extent_ptr *ptr;
		unsigned size_ondisk = e.k->size;
		const char *reason;
		unsigned nonce = UINT_MAX;

		extent_for_each_entry(e, entry) {
			if (__extent_entry_type(entry) >= BCH_EXTENT_ENTRY_MAX)
				return "invalid extent entry type";

			if (extent_entry_is_crc(entry)) {
				crc = bch2_extent_crc_unpack(e.k, entry_to_crc(entry));

				if (crc.offset + e.k->size >
				    crc.uncompressed_size)
					return "checksum offset + key size > uncompressed size";

				size_ondisk = crc.compressed_size;

				if (!bch2_checksum_type_valid(c, crc.csum_type))
					return "invalid checksum type";

				if (crc.compression_type >= BCH_COMPRESSION_NR)
					return "invalid compression type";

				if (bch2_csum_type_is_encryption(crc.csum_type)) {
					if (nonce == UINT_MAX)
						nonce = crc.offset + crc.nonce;
					else if (nonce != crc.offset + crc.nonce)
						return "incorrect nonce";
				}
			} else {
				ptr = entry_to_ptr(entry);

				reason = extent_ptr_invalid(c, e, &entry->ptr,
							    size_ondisk, false);
				if (reason)
					return reason;
			}
		}

		return NULL;
	}

	case BCH_RESERVATION: {
		struct bkey_s_c_reservation r = bkey_s_c_to_reservation(k);

		if (bkey_val_bytes(k.k) != sizeof(struct bch_reservation))
			return "incorrect value size";

		if (!r.v->nr_replicas || r.v->nr_replicas > BCH_REPLICAS_MAX)
			return "invalid nr_replicas";

		return NULL;
	}

	default:
		return "invalid value type";
	}
}

static void bch2_extent_debugcheck_extent(struct bch_fs *c, struct btree *b,
					  struct bkey_s_c_extent e)
{
	const struct bch_extent_ptr *ptr;
	struct bch_dev *ca;
	struct bucket_mark mark;
	unsigned seq, stale;
	char buf[160];
	bool bad;
	unsigned replicas = 0;

	/*
	 * XXX: we should be doing most/all of these checks at startup time,
	 * where we check bch2_bkey_invalid() in btree_node_read_done()
	 *
	 * But note that we can't check for stale pointers or incorrect gc marks
	 * until after journal replay is done (it might be an extent that's
	 * going to get overwritten during replay)
	 */

	extent_for_each_ptr(e, ptr) {
		ca = bch_dev_bkey_exists(c, ptr->dev);
		replicas++;

		/*
		 * If journal replay hasn't finished, we might be seeing keys
		 * that will be overwritten by the time journal replay is done:
		 */
		if (!test_bit(JOURNAL_REPLAY_DONE, &c->journal.flags))
			continue;

		stale = 0;

		do {
			seq = read_seqcount_begin(&c->gc_pos_lock);
			mark = ptr_bucket_mark(ca, ptr);

			/* between mark and bucket gen */
			smp_rmb();

			stale = ptr_stale(ca, ptr);

			bch2_fs_bug_on(stale && !ptr->cached, c,
					 "stale dirty pointer");

			bch2_fs_bug_on(stale > 96, c,
					 "key too stale: %i",
					 stale);

			if (stale)
				break;

			bad = gc_pos_cmp(c->gc_pos, gc_pos_btree_node(b)) > 0 &&
				(mark.data_type != BCH_DATA_USER ||
				 !(ptr->cached
				   ? mark.cached_sectors
				   : mark.dirty_sectors));
		} while (read_seqcount_retry(&c->gc_pos_lock, seq));

		if (bad)
			goto bad_ptr;
	}

	if (replicas > BCH_REPLICAS_MAX) {
		bch2_bkey_val_to_text(c, btree_node_type(b), buf,
				     sizeof(buf), e.s_c);
		bch2_fs_bug(c,
			"extent key bad (too many replicas: %u): %s",
			replicas, buf);
		return;
	}

	if (!bkey_extent_is_cached(e.k) &&
	    !bch2_bkey_replicas_marked(c, BCH_DATA_USER, e.s_c)) {
		bch2_bkey_val_to_text(c, btree_node_type(b),
				     buf, sizeof(buf), e.s_c);
		bch2_fs_bug(c,
			"extent key bad (replicas not marked in superblock):\n%s",
			buf);
		return;
	}

	return;

bad_ptr:
	bch2_bkey_val_to_text(c, btree_node_type(b), buf,
			     sizeof(buf), e.s_c);
	bch2_fs_bug(c, "extent pointer bad gc mark: %s:\nbucket %zu "
		   "gen %i type %u", buf,
		   PTR_BUCKET_NR(ca, ptr), mark.gen, mark.data_type);
	return;
}

void bch2_extent_debugcheck(struct bch_fs *c, struct btree *b, struct bkey_s_c k)
{
	switch (k.k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		bch2_extent_debugcheck_extent(c, b, bkey_s_c_to_extent(k));
		break;
	case BCH_RESERVATION:
		break;
	default:
		BUG();
	}
}

int bch2_extent_to_text(struct bch_fs *c, char *buf,
			size_t size, struct bkey_s_c k)
{
	char *out = buf, *end = buf + size;
	const char *invalid;

#define p(...)	(out += scnprintf(out, end - out, __VA_ARGS__))

	if (bkey_extent_is_data(k.k))
		out += extent_print_ptrs(c, buf, size, bkey_s_c_to_extent(k));

	invalid = bch2_extent_invalid(c, k);
	if (invalid)
		p(" invalid: %s", invalid);
#undef p
	return out - buf;
}

static void bch2_extent_crc_init(union bch_extent_crc *crc,
				 struct bch_extent_crc_unpacked new)
{
#define common_fields(_crc)						\
		.csum_type		= _crc.csum_type,		\
		.compression_type	= _crc.compression_type,	\
		._compressed_size	= _crc.compressed_size - 1,	\
		._uncompressed_size	= _crc.uncompressed_size - 1,	\
		.offset			= _crc.offset

	if (bch_crc_bytes[new.csum_type]	<= 4 &&
	    new.uncompressed_size		<= CRC32_SIZE_MAX &&
	    new.nonce				<= CRC32_NONCE_MAX) {
		crc->crc32 = (struct bch_extent_crc32) {
			.type = 1 << BCH_EXTENT_ENTRY_crc32,
			common_fields(new),
			.csum			= *((__le32 *) &new.csum.lo),
		};
		return;
	}

	if (bch_crc_bytes[new.csum_type]	<= 10 &&
	    new.uncompressed_size		<= CRC64_SIZE_MAX &&
	    new.nonce				<= CRC64_NONCE_MAX) {
		crc->crc64 = (struct bch_extent_crc64) {
			.type = 1 << BCH_EXTENT_ENTRY_crc64,
			common_fields(new),
			.nonce			= new.nonce,
			.csum_lo		= new.csum.lo,
			.csum_hi		= *((__le16 *) &new.csum.hi),
		};
		return;
	}

	if (bch_crc_bytes[new.csum_type]	<= 16 &&
	    new.uncompressed_size		<= CRC128_SIZE_MAX &&
	    new.nonce				<= CRC128_NONCE_MAX) {
		crc->crc128 = (struct bch_extent_crc128) {
			.type = 1 << BCH_EXTENT_ENTRY_crc128,
			common_fields(new),
			.nonce			= new.nonce,
			.csum			= new.csum,
		};
		return;
	}
#undef common_fields
	BUG();
}

void bch2_extent_crc_append(struct bkey_i_extent *e,
			    struct bch_extent_crc_unpacked new)
{
	struct bch_extent_crc_unpacked crc;
	const union bch_extent_entry *i;

	BUG_ON(new.compressed_size > new.uncompressed_size);
	BUG_ON(new.live_size != e->k.size);
	BUG_ON(!new.compressed_size || !new.uncompressed_size);

	/*
	 * Look up the last crc entry, so we can check if we need to add
	 * another:
	 */
	extent_for_each_crc(extent_i_to_s(e), crc, i)
		;

	if (!bch2_crc_unpacked_cmp(crc, new))
		return;

	bch2_extent_crc_init((void *) extent_entry_last(extent_i_to_s(e)), new);
	__extent_entry_push(e);
}

/*
 * bch_extent_normalize - clean up an extent, dropping stale pointers etc.
 *
 * Returns true if @k should be dropped entirely
 *
 * For existing keys, only called when btree nodes are being rewritten, not when
 * they're merely being compacted/resorted in memory.
 */
bool bch2_extent_normalize(struct bch_fs *c, struct bkey_s k)
{
	struct bkey_s_extent e;

	switch (k.k->type) {
	case KEY_TYPE_ERROR:
		return false;

	case KEY_TYPE_DELETED:
		return true;
	case KEY_TYPE_DISCARD:
		return bversion_zero(k.k->version);
	case KEY_TYPE_COOKIE:
		return false;

	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		e = bkey_s_to_extent(k);

		bch2_extent_drop_stale(c, e);

		if (!bkey_val_u64s(e.k)) {
			if (bkey_extent_is_cached(e.k)) {
				k.k->type = KEY_TYPE_DISCARD;
				if (bversion_zero(k.k->version))
					return true;
			} else {
				k.k->type = KEY_TYPE_ERROR;
			}
		}

		return false;
	case BCH_RESERVATION:
		return false;
	default:
		BUG();
	}
}

void bch2_extent_mark_replicas_cached(struct bch_fs *c,
				      struct bkey_s_extent e,
				      unsigned target,
				      unsigned nr_desired_replicas)
{
	struct bch_extent_ptr *ptr;
	int extra = bch2_extent_durability(c, e.c) - nr_desired_replicas;

	if (target && extra > 0)
		extent_for_each_ptr(e, ptr) {
			int n = bch2_extent_ptr_durability(c, ptr);

			if (n && n <= extra &&
			    !bch2_dev_in_target(c, ptr->dev, target)) {
				ptr->cached = true;
				extra -= n;
			}
		}

	if (extra > 0)
		extent_for_each_ptr(e, ptr) {
			int n = bch2_extent_ptr_durability(c, ptr);

			if (n && n <= extra) {
				ptr->cached = true;
				extra -= n;
			}
		}
}

/*
 * This picks a non-stale pointer, preferably from a device other than @avoid.
 * Avoid can be NULL, meaning pick any. If there are no non-stale pointers to
 * other devices, it will still pick a pointer from avoid.
 */
int bch2_extent_pick_ptr(struct bch_fs *c, struct bkey_s_c k,
			 struct bch_devs_mask *avoid,
			 struct extent_pick_ptr *pick)
{
	int ret;

	switch (k.k->type) {
	case KEY_TYPE_ERROR:
		return -EIO;

	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		ret = extent_pick_read_device(c, bkey_s_c_to_extent(k),
					      avoid, pick);

		if (!ret && !bkey_extent_is_cached(k.k))
			ret = -EIO;

		return ret;

	default:
		return 0;
	}
}

enum merge_result bch2_extent_merge(struct bch_fs *c, struct btree *b,
				    struct bkey_i *l, struct bkey_i *r)
{
	struct bkey_s_extent el, er;
	union bch_extent_entry *en_l, *en_r;

	if (key_merging_disabled(c))
		return BCH_MERGE_NOMERGE;

	/*
	 * Generic header checks
	 * Assumes left and right are in order
	 * Left and right must be exactly aligned
	 */

	if (l->k.u64s		!= r->k.u64s ||
	    l->k.type		!= r->k.type ||
	    bversion_cmp(l->k.version, r->k.version) ||
	    bkey_cmp(l->k.p, bkey_start_pos(&r->k)))
		return BCH_MERGE_NOMERGE;

	switch (l->k.type) {
	case KEY_TYPE_DISCARD:
	case KEY_TYPE_ERROR:
		/* These types are mergeable, and no val to check */
		break;

	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		el = bkey_i_to_s_extent(l);
		er = bkey_i_to_s_extent(r);

		extent_for_each_entry(el, en_l) {
			struct bch_extent_ptr *lp, *rp;
			struct bch_dev *ca;

			en_r = vstruct_idx(er.v, (u64 *) en_l - el.v->_data);

			if ((extent_entry_type(en_l) !=
			     extent_entry_type(en_r)) ||
			    extent_entry_is_crc(en_l))
				return BCH_MERGE_NOMERGE;

			lp = &en_l->ptr;
			rp = &en_r->ptr;

			if (lp->offset + el.k->size	!= rp->offset ||
			    lp->dev			!= rp->dev ||
			    lp->gen			!= rp->gen)
				return BCH_MERGE_NOMERGE;

			/* We don't allow extents to straddle buckets: */
			ca = bch_dev_bkey_exists(c, lp->dev);

			if (PTR_BUCKET_NR(ca, lp) != PTR_BUCKET_NR(ca, rp))
				return BCH_MERGE_NOMERGE;
		}

		break;
	case BCH_RESERVATION: {
		struct bkey_i_reservation *li = bkey_i_to_reservation(l);
		struct bkey_i_reservation *ri = bkey_i_to_reservation(r);

		if (li->v.generation != ri->v.generation ||
		    li->v.nr_replicas != ri->v.nr_replicas)
			return BCH_MERGE_NOMERGE;
		break;
	}
	default:
		return BCH_MERGE_NOMERGE;
	}

	l->k.needs_whiteout |= r->k.needs_whiteout;

	/* Keys with no pointers aren't restricted to one bucket and could
	 * overflow KEY_SIZE
	 */
	if ((u64) l->k.size + r->k.size > KEY_SIZE_MAX) {
		bch2_key_resize(&l->k, KEY_SIZE_MAX);
		bch2_cut_front(l->k.p, r);
		return BCH_MERGE_PARTIAL;
	}

	bch2_key_resize(&l->k, l->k.size + r->k.size);

	return BCH_MERGE_MERGE;
}

/*
 * When merging an extent that we're inserting into a btree node, the new merged
 * extent could overlap with an existing 0 size extent - if we don't fix that,
 * it'll break the btree node iterator so this code finds those 0 size extents
 * and shifts them out of the way.
 *
 * Also unpacks and repacks.
 */
static bool bch2_extent_merge_inline(struct bch_fs *c,
				     struct btree_iter *iter,
				     struct bkey_packed *l,
				     struct bkey_packed *r,
				     bool back_merge)
{
	struct btree *b = iter->l[0].b;
	struct btree_node_iter *node_iter = &iter->l[0].iter;
	BKEY_PADDED(k) li, ri;
	struct bkey_packed *m	= back_merge ? l : r;
	struct bkey_i *mi	= back_merge ? &li.k : &ri.k;
	struct bset_tree *t	= bch2_bkey_to_bset(b, m);
	enum merge_result ret;

	EBUG_ON(bkey_written(b, m));

	/*
	 * We need to save copies of both l and r, because we might get a
	 * partial merge (which modifies both) and then fails to repack
	 */
	bch2_bkey_unpack(b, &li.k, l);
	bch2_bkey_unpack(b, &ri.k, r);

	ret = bch2_extent_merge(c, b, &li.k, &ri.k);
	if (ret == BCH_MERGE_NOMERGE)
		return false;

	/*
	 * check if we overlap with deleted extents - would break the sort
	 * order:
	 */
	if (back_merge) {
		struct bkey_packed *n = bkey_next(m);

		if (n != btree_bkey_last(b, t) &&
		    bkey_cmp_left_packed(b, n, &li.k.k.p) <= 0 &&
		    bkey_deleted(n))
			return false;
	} else if (ret == BCH_MERGE_MERGE) {
		struct bkey_packed *prev = bch2_bkey_prev_all(b, t, m);

		if (prev &&
		    bkey_cmp_left_packed_byval(b, prev,
				bkey_start_pos(&li.k.k)) > 0)
			return false;
	}

	if (ret == BCH_MERGE_PARTIAL) {
		if (!extent_i_save(b, m, mi))
			return false;

		if (!back_merge)
			bkey_copy(packed_to_bkey(l), &li.k);
		else
			bkey_copy(packed_to_bkey(r), &ri.k);
	} else {
		if (!extent_i_save(b, m, &li.k))
			return false;
	}

	bch2_bset_fix_invalidated_key(b, t, m);
	bch2_btree_node_iter_fix(iter, b, node_iter,
				 t, m, m->u64s, m->u64s);
	verify_modified_extent(iter, m);

	return ret == BCH_MERGE_MERGE;
}

int bch2_check_range_allocated(struct bch_fs *c, struct bpos pos, u64 size)
{
	struct btree_iter iter;
	struct bpos end = pos;
	struct bkey_s_c k;
	int ret = 0;

	end.offset += size;

	for_each_btree_key(&iter, c, BTREE_ID_EXTENTS, pos,
			     BTREE_ITER_SLOTS, k) {
		if (bkey_cmp(bkey_start_pos(k.k), end) >= 0)
			break;

		if (!bch2_extent_is_fully_allocated(k)) {
			ret = -ENOSPC;
			break;
		}
	}
	bch2_btree_iter_unlock(&iter);

	return ret;
}
