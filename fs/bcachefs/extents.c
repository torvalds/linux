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

	heap_resort(iter, key_sort_cmp, NULL);

	while (!bch2_btree_node_iter_large_end(iter)) {
		if (!should_drop_next_key(iter, b)) {
			struct bkey_packed *k =
				__btree_node_offset_to_key(b, iter->data->k);

			bkey_copy(out, k);
			btree_keys_account_key_add(&nr, 0, out);
			out = bkey_next(out);
		}

		sort_key_next(iter, b, iter->data);
		heap_sift_down(iter, 0, key_sort_cmp, NULL);
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

void bch2_extent_drop_device(struct bkey_s_extent e, unsigned dev)
{
	struct bch_extent_ptr *ptr;

	bch2_extent_drop_ptrs(e, ptr, ptr->dev == dev);
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
	unsigned ret = 0;

	switch (k.k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED: {
		struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;

		extent_for_each_ptr_decode(e, p, entry)
			if (!p.ptr.cached &&
			    p.crc.compression_type != BCH_COMPRESSION_NONE &&
			    p.crc.compressed_size < p.crc.live_size)
				ret += p.crc.compressed_size;
	}
	}

	return ret;
}

bool bch2_extent_matches_ptr(struct bch_fs *c, struct bkey_s_c_extent e,
			     struct bch_extent_ptr m, u64 offset)
{
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;

	extent_for_each_ptr_decode(e, p, entry)
		if (p.ptr.dev	== m.dev &&
		    p.ptr.gen	== m.gen &&
		    (s64) p.ptr.offset + p.crc.offset - bkey_start_offset(e.k) ==
		    (s64) m.offset  - offset)
			return true;

	return false;
}

union bch_extent_entry *bch2_extent_drop_ptr(struct bkey_s_extent e,
					     struct bch_extent_ptr *ptr)
{
	union bch_extent_entry *dst;
	union bch_extent_entry *src;

	EBUG_ON(ptr < &e.v->start->ptr ||
		ptr >= &extent_entry_last(e)->ptr);
	EBUG_ON(ptr->type != 1 << BCH_EXTENT_ENTRY_ptr);

	src = to_entry(ptr + 1);

	if (src != extent_entry_last(e) &&
	    extent_entry_type(src) == BCH_EXTENT_ENTRY_ptr) {
		dst = to_entry(ptr);
	} else {
		extent_for_each_entry(e, dst) {
			if (dst == to_entry(ptr))
				break;

			if (extent_entry_next(dst) == to_entry(ptr) &&
			    extent_entry_is_crc(dst))
				break;
		}
	}

	memmove_u64s_down(dst, src,
			  (u64 *) extent_entry_last(e) - (u64 *) src);
	e.k->u64s -= (u64 *) src - (u64 *) dst;

	return dst;
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
	struct extent_ptr_decoded p;
	union bch_extent_entry *i;
	bool ret = false;

	/* Find a checksum entry that covers only live data: */
	if (!n.csum_type) {
		extent_for_each_crc(extent_i_to_s(e), u, i)
			if (!u.compression_type &&
			    u.csum_type &&
			    u.live_size == u.uncompressed_size) {
				n = u;
				goto found;
			}
		return false;
	}
found:
	BUG_ON(n.compression_type);
	BUG_ON(n.offset);
	BUG_ON(n.live_size != e->k.size);

restart_narrow_pointers:
	extent_for_each_ptr_decode(extent_i_to_s(e), p, i)
		if (can_narrow_crc(p.crc, n)) {
			bch2_extent_drop_ptr(extent_i_to_s(e), &i->ptr);
			p.ptr.offset += p.crc.offset;
			p.crc = n;
			bch2_extent_ptr_decoded_append(e, &p);
			ret = true;
			goto restart_narrow_pointers;
		}

	return ret;
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

static void bch2_extent_drop_stale(struct bch_fs *c, struct bkey_s_extent e)
{
	struct bch_extent_ptr *ptr;

	bch2_extent_drop_ptrs(e, ptr,
		ptr->cached &&
		ptr_stale(bch_dev_bkey_exists(c, ptr->dev), ptr));
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
			case BCH_EXTENT_ENTRY_ptr:
				break;
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

static void extent_print_ptrs(struct printbuf *out, struct bch_fs *c,
			      struct bkey_s_c_extent e)
{
	const union bch_extent_entry *entry;
	struct bch_extent_crc_unpacked crc;
	const struct bch_extent_ptr *ptr;
	struct bch_dev *ca;
	bool first = true;

	extent_for_each_entry(e, entry) {
		if (!first)
			pr_buf(out, " ");

		switch (__extent_entry_type(entry)) {
		case BCH_EXTENT_ENTRY_crc32:
		case BCH_EXTENT_ENTRY_crc64:
		case BCH_EXTENT_ENTRY_crc128:
			crc = bch2_extent_crc_unpack(e.k, entry_to_crc(entry));

			pr_buf(out, "crc: c_size %u size %u offset %u nonce %u csum %u compress %u",
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

			pr_buf(out, "ptr: %u:%llu gen %u%s%s", ptr->dev,
			       (u64) ptr->offset, ptr->gen,
			       ptr->cached ? " cached" : "",
			       ca && ptr_stale(ca, ptr)
			       ? " stale" : "");
			break;
		default:
			pr_buf(out, "(invalid extent entry %.16llx)", *((u64 *) entry));
			goto out;
		}

		first = false;
	}
out:
	if (bkey_extent_is_cached(e.k))
		pr_buf(out, " cached");
}

static struct bch_dev_io_failures *dev_io_failures(struct bch_io_failures *f,
						   unsigned dev)
{
	struct bch_dev_io_failures *i;

	for (i = f->devs; i < f->devs + f->nr; i++)
		if (i->dev == dev)
			return i;

	return NULL;
}

void bch2_mark_io_failure(struct bch_io_failures *failed,
			  struct extent_ptr_decoded *p)
{
	struct bch_dev_io_failures *f = dev_io_failures(failed, p->ptr.dev);

	if (!f) {
		BUG_ON(failed->nr >= ARRAY_SIZE(failed->devs));

		f = &failed->devs[failed->nr++];
		f->dev		= p->ptr.dev;
		f->nr_failed	= 1;
		f->nr_retries	= 0;
	} else {
		f->nr_failed++;
	}
}

/*
 * returns true if p1 is better than p2:
 */
static inline bool ptr_better(struct bch_fs *c,
			      const struct extent_ptr_decoded p1,
			      const struct extent_ptr_decoded p2)
{
	struct bch_dev *dev1 = bch_dev_bkey_exists(c, p1.ptr.dev);
	struct bch_dev *dev2 = bch_dev_bkey_exists(c, p2.ptr.dev);

	u64 l1 = atomic64_read(&dev1->cur_latency[READ]);
	u64 l2 = atomic64_read(&dev2->cur_latency[READ]);

	/* Pick at random, biased in favor of the faster device: */

	return bch2_rand_range(l1 + l2) > l1;
}

static int extent_pick_read_device(struct bch_fs *c,
				   struct bkey_s_c_extent e,
				   struct bch_io_failures *failed,
				   struct extent_ptr_decoded *pick)
{
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	struct bch_dev_io_failures *f;
	struct bch_dev *ca;
	int ret = 0;

	extent_for_each_ptr_decode(e, p, entry) {
		ca = bch_dev_bkey_exists(c, p.ptr.dev);

		if (p.ptr.cached && ptr_stale(ca, &p.ptr))
			continue;

		f = failed ? dev_io_failures(failed, p.ptr.dev) : NULL;
		if (f && f->nr_failed >= f->nr_retries)
			continue;

		if (ret && !ptr_better(c, p, *pick))
			continue;

		*pick = p;
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

	if (!test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) &&
	    !bch2_bkey_replicas_marked(c, btree_node_type(b), e.s_c)) {
		bch2_bkey_val_to_text(&PBUF(buf), c, btree_node_type(b), k);
		bch2_fs_bug(c,
			"btree key bad (replicas not marked in superblock):\n%s",
			buf);
		return;
	}

	return;
err:
	bch2_bkey_val_to_text(&PBUF(buf), c, btree_node_type(b), k);
	bch2_fs_bug(c, "%s btree pointer %s: bucket %zi gen %i mark %08x",
		    err, buf, PTR_BUCKET_NR(ca, ptr),
		    mark.gen, (unsigned) mark.v.counter);
}

void bch2_btree_ptr_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	const char *invalid;

	if (bkey_extent_is_data(k.k))
		extent_print_ptrs(out, c, bkey_s_c_to_extent(k));

	invalid = bch2_btree_ptr_invalid(c, k);
	if (invalid)
		pr_buf(out, " invalid: %s", invalid);
}

int bch2_btree_pick_ptr(struct bch_fs *c, const struct btree *b,
			struct bch_io_failures *failed,
			struct extent_ptr_decoded *pick)
{
	return extent_pick_read_device(c, bkey_i_to_s_c_extent(&b->key),
				       failed, pick);
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
	heap_sift_down(iter, i, extent_sort_cmp, NULL);
}

static inline void extent_sort_next(struct btree_node_iter_large *iter,
				    struct btree *b,
				    struct btree_node_iter_set *i)
{
	sort_key_next(iter, b, i);
	heap_sift_down(iter, i - iter->data, extent_sort_cmp, NULL);
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

	heap_resort(iter, extent_sort_cmp, NULL);

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

	/* for deleting: */
	struct bkey_i			whiteout;
	bool				update_journal;
	bool				update_btree;
	bool				deleting;
};

static bool bch2_extent_merge_inline(struct bch_fs *,
				     struct btree_iter *,
				     struct bkey_packed *,
				     struct bkey_packed *,
				     bool);

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

		bch2_bkey_to_text(&PBUF(buf1), &insert->k);
		bch2_bkey_to_text(&PBUF(buf2), &uk);

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
	struct btree_node_iter node_iter;
	struct bkey_packed *k;

	BUG_ON(insert->k.u64s > bch_btree_keys_u64s_remaining(c, l->b));

	EBUG_ON(bkey_deleted(&insert->k) || !insert->k.size);
	verify_extent_nonoverlapping(l->b, &l->iter, insert);

	node_iter = l->iter;
	k = bch2_btree_node_iter_prev_filter(&node_iter, l->b, KEY_TYPE_DISCARD);
	if (k && !bkey_written(l->b, k) &&
	    bch2_extent_merge_inline(c, iter, k, bkey_to_packed(insert), true))
		return;

	node_iter = l->iter;
	k = bch2_btree_node_iter_peek_filter(&node_iter, l->b, KEY_TYPE_DISCARD);
	if (k && !bkey_written(l->b, k) &&
	    bch2_extent_merge_inline(c, iter, bkey_to_packed(insert), k, false))
		return;

	k = bch2_btree_node_iter_bset_pos(&l->iter, l->b, bset_tree_last(l->b));

	bch2_bset_insert(l->b, &l->iter, k, insert, 0);
	bch2_btree_node_iter_fix(iter, l->b, &l->iter, k, 0, k->u64s);
	bch2_btree_iter_verify(iter, l->b);
}

static void extent_insert_committed(struct extent_insert_state *s)
{
	struct bch_fs *c = s->trans->c;
	struct btree_iter *iter = s->insert->iter;
	struct bkey_i *insert = s->insert->k;
	BKEY_PADDED(k) split;

	EBUG_ON(bkey_cmp(insert->k.p, s->committed) < 0);
	EBUG_ON(bkey_cmp(s->committed, bkey_start_pos(&insert->k)) < 0);

	bkey_copy(&split.k, insert);
	if (s->deleting)
		split.k.k.type = KEY_TYPE_DISCARD;

	bch2_cut_back(s->committed, &split.k.k);

	if (!bkey_cmp(s->committed, iter->pos))
		return;

	bch2_btree_iter_set_pos_same_leaf(iter, s->committed);

	if (s->update_btree) {
		if (debug_check_bkeys(c))
			bch2_bkey_debugcheck(c, iter->l[0].b,
					     bkey_i_to_s_c(&split.k));

		EBUG_ON(bkey_deleted(&split.k.k) || !split.k.k.size);

		extent_bset_insert(c, iter, &split.k);
	}

	if (s->update_journal) {
		bkey_copy(&split.k, !s->deleting ? insert : &s->whiteout);
		if (s->deleting)
			split.k.k.type = KEY_TYPE_DISCARD;

		bch2_cut_back(s->committed, &split.k.k);

		EBUG_ON(bkey_deleted(&split.k.k) || !split.k.k.size);

		bch2_btree_journal_key(s->trans, iter, &split.k);
	}

	bch2_cut_front(s->committed, insert);

	insert->k.needs_whiteout	= false;
	s->trans->did_work		= true;
}

void bch2_extent_trim_atomic(struct bkey_i *k, struct btree_iter *iter)
{
	struct btree *b = iter->l[0].b;

	BUG_ON(iter->uptodate > BTREE_ITER_NEED_PEEK);

	bch2_cut_back(b->key.k.p, &k->k);

	BUG_ON(bkey_cmp(bkey_start_pos(&k->k), b->data->min_key) < 0);
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

	BUG_ON(trans->flags & BTREE_INSERT_ATOMIC &&
	       !bch2_extent_is_atomic(&insert->k->k, insert->iter));

	/*
	 * We avoid creating whiteouts whenever possible when deleting, but
	 * those optimizations mean we may potentially insert two whiteouts
	 * instead of one (when we overlap with the front of one extent and the
	 * back of another):
	 */
	if (bkey_whiteout(&insert->k->k))
		*u64s += BKEY_U64s;

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
				sectors, flags)) {
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
	      struct bkey_packed *_k, struct bkey_s k,
	      enum bch_extent_overlap overlap)
{
	struct bch_fs *c = s->trans->c;
	struct btree_iter *iter = s->insert->iter;
	struct btree_iter_level *l = &iter->l[0];

	switch (overlap) {
	case BCH_EXTENT_OVERLAP_FRONT:
		/* insert overlaps with start of k: */
		__bch2_cut_front(insert->k.p, k);
		BUG_ON(bkey_deleted(k.k));
		extent_save(l->b, _k, k.k);
		verify_modified_extent(iter, _k);
		break;

	case BCH_EXTENT_OVERLAP_BACK:
		/* insert overlaps with end of k: */
		bch2_cut_back(bkey_start_pos(&insert->k), k.k);
		BUG_ON(bkey_deleted(k.k));
		extent_save(l->b, _k, k.k);

		/*
		 * As the auxiliary tree is indexed by the end of the
		 * key and we've just changed the end, update the
		 * auxiliary tree.
		 */
		bch2_bset_fix_invalidated_key(l->b, _k);
		bch2_btree_node_iter_fix(iter, l->b, &l->iter,
					 _k, _k->u64s, _k->u64s);
		verify_modified_extent(iter, _k);
		break;

	case BCH_EXTENT_OVERLAP_ALL: {
		/* The insert key completely covers k, invalidate k */
		if (!bkey_whiteout(k.k))
			btree_account_key_drop(l->b, _k);

		k.k->size = 0;
		k.k->type = KEY_TYPE_DELETED;

		if (_k >= btree_bset_last(l->b)->start) {
			unsigned u64s = _k->u64s;

			bch2_bset_delete(l->b, _k, _k->u64s);
			bch2_btree_node_iter_fix(iter, l->b, &l->iter,
						 _k, u64s, 0);
			bch2_btree_iter_verify(iter, l->b);
		} else {
			extent_save(l->b, _k, k.k);
			bch2_btree_node_iter_fix(iter, l->b, &l->iter,
						 _k, _k->u64s, _k->u64s);
			verify_modified_extent(iter, _k);
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
		split.k.k.needs_whiteout |= bkey_written(l->b, _k);

		bch2_cut_back(bkey_start_pos(&insert->k), &split.k.k);
		BUG_ON(bkey_deleted(&split.k.k));

		__bch2_cut_front(insert->k.p, k);
		BUG_ON(bkey_deleted(k.k));
		extent_save(l->b, _k, k.k);
		verify_modified_extent(iter, _k);

		extent_bset_insert(c, iter, &split.k);
		break;
	}
	}
}

static void __bch2_insert_fixup_extent(struct extent_insert_state *s)
{
	struct btree_iter *iter = s->insert->iter;
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_packed *_k;
	struct bkey unpacked;
	struct bkey_i *insert = s->insert->k;

	while (bkey_cmp(s->committed, insert->k.p) < 0 &&
	       (_k = bch2_btree_node_iter_peek_filter(&l->iter, l->b,
						      KEY_TYPE_DISCARD))) {
		struct bkey_s k = __bkey_disassemble(l->b, _k, &unpacked);
		enum bch_extent_overlap overlap = bch2_extent_overlap(&insert->k, k.k);

		EBUG_ON(bkey_cmp(iter->pos, k.k->p) >= 0);

		if (bkey_cmp(bkey_start_pos(k.k), insert->k.p) >= 0)
			break;

		s->committed = bpos_min(s->insert->k->k.p, k.k->p);

		if (!bkey_whiteout(k.k))
			s->update_journal = true;

		if (!s->update_journal) {
			bch2_cut_front(s->committed, insert);
			bch2_cut_front(s->committed, &s->whiteout);
			bch2_btree_iter_set_pos_same_leaf(iter, s->committed);
			goto next;
		}

		/*
		 * When deleting, if possible just do it by switching the type
		 * of the key we're deleting, instead of creating and inserting
		 * a new whiteout:
		 */
		if (s->deleting &&
		    !s->update_btree &&
		    !bkey_cmp(insert->k.p, k.k->p) &&
		    !bkey_cmp(bkey_start_pos(&insert->k), bkey_start_pos(k.k))) {
			if (!bkey_whiteout(k.k)) {
				btree_account_key_drop(l->b, _k);
				_k->type = KEY_TYPE_DISCARD;
				reserve_whiteout(l->b, _k);
			}
			break;
		}

		if (k.k->needs_whiteout || bkey_written(l->b, _k)) {
			insert->k.needs_whiteout = true;
			s->update_btree = true;
		}

		if (s->update_btree &&
		    overlap == BCH_EXTENT_OVERLAP_ALL &&
		    bkey_whiteout(k.k) &&
		    k.k->needs_whiteout) {
			unreserve_whiteout(l->b, _k);
			_k->needs_whiteout = false;
		}

		extent_squash(s, insert, _k, k, overlap);

		if (!s->update_btree)
			bch2_cut_front(s->committed, insert);
next:
		if (overlap == BCH_EXTENT_OVERLAP_FRONT ||
		    overlap == BCH_EXTENT_OVERLAP_MIDDLE)
			break;
	}

	if (bkey_cmp(s->committed, insert->k.p) < 0)
		s->committed = bpos_min(s->insert->k->k.p, l->b->key.k.p);

	/*
	 * may have skipped past some deleted extents greater than the insert
	 * key, before we got to a non deleted extent and knew we could bail out
	 * rewind the iterator a bit if necessary:
	 */
	{
		struct btree_node_iter node_iter = l->iter;

		while ((_k = bch2_btree_node_iter_prev_all(&node_iter, l->b)) &&
		       bkey_cmp_left_packed(l->b, _k, &s->committed) > 0)
			l->iter = node_iter;
	}
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
	struct btree_iter *iter	= insert->iter;
	struct btree *b		= iter->l[0].b;
	struct extent_insert_state s = {
		.trans		= trans,
		.insert		= insert,
		.committed	= iter->pos,

		.whiteout	= *insert->k,
		.update_journal	= !bkey_whiteout(&insert->k->k),
		.update_btree	= !bkey_whiteout(&insert->k->k),
		.deleting	= bkey_whiteout(&insert->k->k),
	};

	EBUG_ON(iter->level);
	EBUG_ON(!insert->k->k.size);

	/*
	 * As we process overlapping extents, we advance @iter->pos both to
	 * signal to our caller (btree_insert_key()) how much of @insert->k has
	 * been inserted, and also to keep @iter->pos consistent with
	 * @insert->k and the node iterator that we're advancing:
	 */
	EBUG_ON(bkey_cmp(iter->pos, bkey_start_pos(&insert->k->k)));

	__bch2_insert_fixup_extent(&s);

	extent_insert_committed(&s);

	EBUG_ON(bkey_cmp(iter->pos, bkey_start_pos(&insert->k->k)));
	EBUG_ON(bkey_cmp(iter->pos, s.committed));

	if (insert->k->k.size) {
		/* got to the end of this leaf node */
		BUG_ON(bkey_cmp(iter->pos, b->key.k.p));
		return BTREE_INSERT_NEED_TRAVERSE;
	}

	return BTREE_INSERT_OK;
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
		bch2_bkey_val_to_text(&PBUF(buf), c, btree_node_type(b),
				      e.s_c);
		bch2_fs_bug(c,
			"extent key bad (too many replicas: %u): %s",
			replicas, buf);
		return;
	}

	if (!test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) &&
	    !bch2_bkey_replicas_marked(c, btree_node_type(b), e.s_c)) {
		bch2_bkey_val_to_text(&PBUF(buf), c, btree_node_type(b),
				      e.s_c);
		bch2_fs_bug(c,
			"extent key bad (replicas not marked in superblock):\n%s",
			buf);
		return;
	}

	return;

bad_ptr:
	bch2_bkey_val_to_text(&PBUF(buf), c, btree_node_type(b),
			      e.s_c);
	bch2_fs_bug(c, "extent pointer bad gc mark: %s:\nbucket %zu "
		   "gen %i type %u", buf,
		   PTR_BUCKET_NR(ca, ptr), mark.gen, mark.data_type);
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

void bch2_extent_to_text(struct printbuf *out, struct bch_fs *c,
			 struct bkey_s_c k)
{
	const char *invalid;

	if (bkey_extent_is_data(k.k))
		extent_print_ptrs(out, c, bkey_s_c_to_extent(k));

	invalid = bch2_extent_invalid(c, k);
	if (invalid)
		pr_buf(out, " invalid: %s", invalid);
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
	bch2_extent_crc_init((void *) extent_entry_last(extent_i_to_s(e)), new);
	__extent_entry_push(e);
}

static inline void __extent_entry_insert(struct bkey_i_extent *e,
					 union bch_extent_entry *dst,
					 union bch_extent_entry *new)
{
	union bch_extent_entry *end = extent_entry_last(extent_i_to_s(e));

	memmove_u64s_up((u64 *) dst + extent_entry_u64s(new),
			dst, (u64 *) end - (u64 *) dst);
	e->k.u64s += extent_entry_u64s(new);
	memcpy_u64s_small(dst, new, extent_entry_u64s(new));
}

void bch2_extent_ptr_decoded_append(struct bkey_i_extent *e,
				    struct extent_ptr_decoded *p)
{
	struct bch_extent_crc_unpacked crc = bch2_extent_crc_unpack(&e->k, NULL);
	union bch_extent_entry *pos;

	if (!bch2_crc_unpacked_cmp(crc, p->crc)) {
		pos = e->v.start;
		goto found;
	}

	extent_for_each_crc(extent_i_to_s(e), crc, pos)
		if (!bch2_crc_unpacked_cmp(crc, p->crc)) {
			pos = extent_entry_next(pos);
			goto found;
		}

	bch2_extent_crc_append(e, p->crc);
	pos = extent_entry_last(extent_i_to_s(e));
found:
	p->ptr.type = 1 << BCH_EXTENT_ENTRY_ptr;
	__extent_entry_insert(e, pos, to_entry(&p->ptr));
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
			 struct bch_io_failures *failed,
			 struct extent_ptr_decoded *pick)
{
	int ret;

	switch (k.k->type) {
	case KEY_TYPE_ERROR:
		return -EIO;

	case BCH_EXTENT:
	case BCH_EXTENT_CACHED:
		ret = extent_pick_read_device(c, bkey_s_c_to_extent(k),
					      failed, pick);

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

	bch2_bset_fix_invalidated_key(b, m);
	bch2_btree_node_iter_fix(iter, b, node_iter,
				 m, m->u64s, m->u64s);
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
