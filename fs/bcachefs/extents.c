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

unsigned bch2_bkey_nr_ptrs(struct bkey_s_c k)
{
	struct bkey_ptrs_c p = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;
	unsigned nr_ptrs = 0;

	bkey_for_each_ptr(p, ptr)
		nr_ptrs++;

	return nr_ptrs;
}

unsigned bch2_bkey_nr_dirty_ptrs(struct bkey_s_c k)
{
	unsigned nr_ptrs = 0;

	switch (k.k->type) {
	case KEY_TYPE_btree_ptr:
	case KEY_TYPE_extent: {
		struct bkey_ptrs_c p = bch2_bkey_ptrs_c(k);
		const struct bch_extent_ptr *ptr;

		bkey_for_each_ptr(p, ptr)
			nr_ptrs += !ptr->cached;
		BUG_ON(!nr_ptrs);
		break;
	}
	case KEY_TYPE_reservation:
		nr_ptrs = bkey_s_c_to_reservation(k).v->nr_replicas;
		break;
	}

	return nr_ptrs;
}

static unsigned bch2_extent_ptr_durability(struct bch_fs *c,
					   struct extent_ptr_decoded p)
{
	unsigned i, durability = 0;
	struct bch_dev *ca;

	if (p.ptr.cached)
		return 0;

	ca = bch_dev_bkey_exists(c, p.ptr.dev);

	if (ca->mi.state != BCH_MEMBER_STATE_FAILED)
		durability = max_t(unsigned, durability, ca->mi.durability);

	for (i = 0; i < p.ec_nr; i++) {
		struct stripe *s =
			genradix_ptr(&c->stripes[0], p.idx);

		if (WARN_ON(!s))
			continue;

		durability = max_t(unsigned, durability, s->nr_redundant);
	}

	return durability;
}

unsigned bch2_bkey_durability(struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned durability = 0;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
		durability += bch2_extent_ptr_durability(c, p);

	return durability;
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
		f->idx		= p->idx;
		f->nr_failed	= 1;
		f->nr_retries	= 0;
	} else if (p->idx != f->idx) {
		f->idx		= p->idx;
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
	if (likely(!p1.idx && !p2.idx)) {
		struct bch_dev *dev1 = bch_dev_bkey_exists(c, p1.ptr.dev);
		struct bch_dev *dev2 = bch_dev_bkey_exists(c, p2.ptr.dev);

		u64 l1 = atomic64_read(&dev1->cur_latency[READ]);
		u64 l2 = atomic64_read(&dev2->cur_latency[READ]);

		/* Pick at random, biased in favor of the faster device: */

		return bch2_rand_range(l1 + l2) > l1;
	}

	if (force_reconstruct_read(c))
		return p1.idx > p2.idx;

	return p1.idx < p2.idx;
}

/*
 * This picks a non-stale pointer, preferably from a device other than @avoid.
 * Avoid can be NULL, meaning pick any. If there are no non-stale pointers to
 * other devices, it will still pick a pointer from avoid.
 */
int bch2_bkey_pick_read_device(struct bch_fs *c, struct bkey_s_c k,
			       struct bch_io_failures *failed,
			       struct extent_ptr_decoded *pick)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	struct bch_dev_io_failures *f;
	struct bch_dev *ca;
	int ret = 0;

	if (k.k->type == KEY_TYPE_error)
		return -EIO;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		ca = bch_dev_bkey_exists(c, p.ptr.dev);

		/*
		 * If there are any dirty pointers it's an error if we can't
		 * read:
		 */
		if (!ret && !p.ptr.cached)
			ret = -EIO;

		if (p.ptr.cached && ptr_stale(ca, &p.ptr))
			continue;

		f = failed ? dev_io_failures(failed, p.ptr.dev) : NULL;
		if (f)
			p.idx = f->nr_failed < f->nr_retries
				? f->idx
				: f->idx + 1;

		if (!p.idx &&
		    !bch2_dev_is_readable(ca))
			p.idx++;

		if (force_reconstruct_read(c) &&
		    !p.idx && p.ec_nr)
			p.idx++;

		if (p.idx >= p.ec_nr + 1)
			continue;

		if (ret > 0 && !ptr_better(c, p, *pick))
			continue;

		*pick = p;
		ret = 1;
	}

	return ret;
}

void bch2_bkey_append_ptr(struct bkey_i *k,
			  struct bch_extent_ptr ptr)
{
	EBUG_ON(bch2_bkey_has_device(bkey_i_to_s_c(k), ptr.dev));

	switch (k->k.type) {
	case KEY_TYPE_btree_ptr:
	case KEY_TYPE_extent:
		EBUG_ON(bkey_val_u64s(&k->k) >= BKEY_EXTENT_VAL_U64s_MAX);

		ptr.type = 1 << BCH_EXTENT_ENTRY_ptr;

		memcpy((void *) &k->v + bkey_val_bytes(&k->k),
		       &ptr,
		       sizeof(ptr));
		k->u64s++;
		break;
	default:
		BUG();
	}
}

void bch2_bkey_drop_device(struct bkey_s k, unsigned dev)
{
	struct bch_extent_ptr *ptr;

	bch2_bkey_drop_ptrs(k, ptr, ptr->dev == dev);
}

/* extent specific utility code */

const struct bch_extent_ptr *
bch2_extent_has_device(struct bkey_s_c_extent e, unsigned dev)
{
	const struct bch_extent_ptr *ptr;

	extent_for_each_ptr(e, ptr)
		if (ptr->dev == dev)
			return ptr;

	return NULL;
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

unsigned bch2_extent_is_compressed(struct bkey_s_c k)
{
	unsigned ret = 0;

	switch (k.k->type) {
	case KEY_TYPE_extent: {
		struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;

		extent_for_each_ptr_decode(e, p, entry)
			if (!p.ptr.cached &&
			    p.crc.compression_type != BCH_COMPRESSION_NONE)
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

static union bch_extent_entry *extent_entry_prev(struct bkey_ptrs ptrs,
					  union bch_extent_entry *entry)
{
	union bch_extent_entry *i = ptrs.start;

	if (i == entry)
		return NULL;

	while (extent_entry_next(i) != entry)
		i = extent_entry_next(i);
	return i;
}

union bch_extent_entry *bch2_bkey_drop_ptr(struct bkey_s k,
					   struct bch_extent_ptr *ptr)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(k);
	union bch_extent_entry *dst, *src, *prev;
	bool drop_crc = true;

	EBUG_ON(ptr < &ptrs.start->ptr ||
		ptr >= &ptrs.end->ptr);
	EBUG_ON(ptr->type != 1 << BCH_EXTENT_ENTRY_ptr);

	src = extent_entry_next(to_entry(ptr));
	if (src != ptrs.end &&
	    !extent_entry_is_crc(src))
		drop_crc = false;

	dst = to_entry(ptr);
	while ((prev = extent_entry_prev(ptrs, dst))) {
		if (extent_entry_is_ptr(prev))
			break;

		if (extent_entry_is_crc(prev)) {
			if (drop_crc)
				dst = prev;
			break;
		}

		dst = prev;
	}

	memmove_u64s_down(dst, src,
			  (u64 *) ptrs.end - (u64 *) src);
	k.k->u64s -= (u64 *) src - (u64 *) dst;

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
			bch2_bkey_drop_ptr(extent_i_to_s(e).s, &i->ptr);
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

void bch2_ptr_swab(const struct bkey_format *f, struct bkey_packed *k)
{
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
		case BCH_EXTENT_ENTRY_stripe_ptr:
			break;
		}
	}
}

static const char *extent_ptr_invalid(const struct bch_fs *c,
				      struct bkey_s_c k,
				      const struct bch_extent_ptr *ptr,
				      unsigned size_ondisk,
				      bool metadata)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr2;
	struct bch_dev *ca;

	if (ptr->dev >= c->sb.nr_devices ||
	    !c->devs[ptr->dev])
		return "pointer to invalid device";

	ca = bch_dev_bkey_exists(c, ptr->dev);
	if (!ca)
		return "pointer to invalid device";

	bkey_for_each_ptr(ptrs, ptr2)
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

static void bkey_ptrs_to_text(struct printbuf *out, struct bch_fs *c,
			      struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct bch_extent_crc_unpacked crc;
	const struct bch_extent_ptr *ptr;
	const struct bch_extent_stripe_ptr *ec;
	struct bch_dev *ca;
	bool first = true;

	bkey_extent_entry_for_each(ptrs, entry) {
		if (!first)
			pr_buf(out, " ");

		switch (__extent_entry_type(entry)) {
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
		case BCH_EXTENT_ENTRY_crc32:
		case BCH_EXTENT_ENTRY_crc64:
		case BCH_EXTENT_ENTRY_crc128:
			crc = bch2_extent_crc_unpack(k.k, entry_to_crc(entry));

			pr_buf(out, "crc: c_size %u size %u offset %u nonce %u csum %u compress %u",
			       crc.compressed_size,
			       crc.uncompressed_size,
			       crc.offset, crc.nonce,
			       crc.csum_type,
			       crc.compression_type);
			break;
		case BCH_EXTENT_ENTRY_stripe_ptr:
			ec = &entry->stripe_ptr;

			pr_buf(out, "ec: idx %llu block %u",
			       (u64) ec->idx, ec->block);
			break;
		default:
			pr_buf(out, "(invalid extent entry %.16llx)", *((u64 *) entry));
			return;
		}

		first = false;
	}
}

/* Btree ptrs */

const char *bch2_btree_ptr_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	const struct bch_extent_ptr *ptr;
	const char *reason;

	if (bkey_val_u64s(k.k) > BKEY_BTREE_PTR_VAL_U64s_MAX)
		return "value too big";

	bkey_extent_entry_for_each(ptrs, entry) {
		if (__extent_entry_type(entry) >= BCH_EXTENT_ENTRY_MAX)
			return "invalid extent entry type";

		if (!extent_entry_is_ptr(entry))
			return "has non ptr field";
	}

	bkey_for_each_ptr(ptrs, ptr) {
		reason = extent_ptr_invalid(c, k, ptr,
					    c->opts.btree_node_size,
					    true);
		if (reason)
			return reason;
	}

	return NULL;
}

void bch2_btree_ptr_debugcheck(struct bch_fs *c, struct btree *b,
			       struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;
	const char *err;
	char buf[160];
	struct bucket_mark mark;
	struct bch_dev *ca;

	bch2_fs_bug_on(!test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) &&
		       !bch2_bkey_replicas_marked(c, k, false), c,
		       "btree key bad (replicas not marked in superblock):\n%s",
		       (bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));

	if (!test_bit(BCH_FS_INITIAL_GC_DONE, &c->flags))
		return;

	bkey_for_each_ptr(ptrs, ptr) {
		ca = bch_dev_bkey_exists(c, ptr->dev);

		mark = ptr_bucket_mark(ca, ptr);

		err = "stale";
		if (gen_after(mark.gen, ptr->gen))
			goto err;

		err = "inconsistent";
		if (mark.data_type != BCH_DATA_BTREE ||
		    mark.dirty_sectors < c->opts.btree_node_size)
			goto err;
	}

	return;
err:
	bch2_bkey_val_to_text(&PBUF(buf), c, k);
	bch2_fs_bug(c, "%s btree pointer %s: bucket %zi gen %i mark %08x",
		    err, buf, PTR_BUCKET_NR(ca, ptr),
		    mark.gen, (unsigned) mark.v.counter);
}

void bch2_btree_ptr_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	const char *invalid;

	bkey_ptrs_to_text(out, c, k);

	invalid = bch2_btree_ptr_invalid(c, k);
	if (invalid)
		pr_buf(out, " invalid: %s", invalid);
}

/* Extents */

bool __bch2_cut_front(struct bpos where, struct bkey_s k)
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
		k.k->type = KEY_TYPE_deleted;
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
			case BCH_EXTENT_ENTRY_stripe_ptr:
				break;
			}

			if (extent_entry_is_crc(entry))
				seen_crc = true;
		}
	}

	k.k->size = len;

	return true;
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
		k->type = KEY_TYPE_deleted;

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
	k = bch2_btree_node_iter_prev_filter(&iter, b, KEY_TYPE_discard);
	BUG_ON(k &&
	       (uk = bkey_unpack_key(b, k),
		bkey_cmp(uk.p, bkey_start_pos(&insert->k)) > 0));

	iter = *_iter;
	k = bch2_btree_node_iter_peek_filter(&iter, b, KEY_TYPE_discard);
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
	k = bch2_btree_node_iter_prev_filter(&node_iter, l->b, KEY_TYPE_discard);
	if (k && !bkey_written(l->b, k) &&
	    bch2_extent_merge_inline(c, iter, k, bkey_to_packed(insert), true))
		return;

	node_iter = l->iter;
	k = bch2_btree_node_iter_peek_filter(&node_iter, l->b, KEY_TYPE_discard);
	if (k && !bkey_written(l->b, k) &&
	    bch2_extent_merge_inline(c, iter, bkey_to_packed(insert), k, false))
		return;

	k = bch2_btree_node_iter_bset_pos(&l->iter, l->b, bset_tree_last(l->b));

	bch2_bset_insert(l->b, &l->iter, k, insert, 0);
	bch2_btree_node_iter_fix(iter, l->b, &l->iter, k, 0, k->u64s);
	bch2_btree_iter_verify(iter, l->b);
}

static inline struct bpos
bch2_extent_atomic_end(struct bkey_i *k, struct btree_iter *iter)
{
	struct btree *b = iter->l[0].b;

	BUG_ON(iter->uptodate > BTREE_ITER_NEED_PEEK);
	BUG_ON(bkey_cmp(bkey_start_pos(&k->k), b->data->min_key) < 0);

	return bpos_min(k->k.p, b->key.k.p);
}

void bch2_extent_trim_atomic(struct bkey_i *k, struct btree_iter *iter)
{
	bch2_cut_back(bch2_extent_atomic_end(k, iter), &k->k);
}

bool bch2_extent_is_atomic(struct bkey_i *k, struct btree_iter *iter)
{
	return !bkey_cmp(bch2_extent_atomic_end(k, iter), k->k.p);
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

	/*
	 * We avoid creating whiteouts whenever possible when deleting, but
	 * those optimizations mean we may potentially insert two whiteouts
	 * instead of one (when we overlap with the front of one extent and the
	 * back of another):
	 */
	if (bkey_whiteout(&insert->k->k))
		*u64s += BKEY_U64s;

	_k = bch2_btree_node_iter_peek_filter(&node_iter, l->b,
					      KEY_TYPE_discard);
	if (!_k)
		return BTREE_INSERT_OK;

	k = bkey_disassemble(l->b, _k, &unpacked);

	overlap = bch2_extent_overlap(&insert->k->k, k.k);

	/* account for having to split existing extent: */
	if (overlap == BCH_EXTENT_OVERLAP_MIDDLE)
		*u64s += _k->u64s;

	if (overlap == BCH_EXTENT_OVERLAP_MIDDLE &&
	    (sectors = bch2_extent_is_compressed(k))) {
		int flags = trans->flags & BTREE_INSERT_NOFAIL
			? BCH_DISK_RESERVATION_NOFAIL : 0;

		switch (bch2_disk_reservation_add(trans->c,
				trans->disk_res,
				sectors, flags)) {
		case 0:
			break;
		case -ENOSPC:
			return BTREE_INSERT_ENOSPC;
		default:
			BUG();
		}
	}

	return BTREE_INSERT_OK;
}

static void
extent_squash(struct bch_fs *c, struct btree_iter *iter,
	      struct bkey_i *insert,
	      struct bkey_packed *_k, struct bkey_s k,
	      enum bch_extent_overlap overlap)
{
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
		k.k->type = KEY_TYPE_deleted;

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

struct extent_insert_state {
	struct bkey_i			whiteout;
	bool				update_journal;
	bool				update_btree;
	bool				deleting;
};

static void __bch2_insert_fixup_extent(struct bch_fs *c,
				       struct btree_iter *iter,
				       struct bkey_i *insert,
				       struct extent_insert_state *s)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_packed *_k;
	struct bkey unpacked;

	while ((_k = bch2_btree_node_iter_peek_filter(&l->iter, l->b,
						      KEY_TYPE_discard))) {
		struct bkey_s k = __bkey_disassemble(l->b, _k, &unpacked);
		struct bpos cur_end = bpos_min(insert->k.p, k.k->p);
		enum bch_extent_overlap overlap =
			bch2_extent_overlap(&insert->k, k.k);

		if (bkey_cmp(bkey_start_pos(k.k), insert->k.p) >= 0)
			break;

		if (!bkey_whiteout(k.k))
			s->update_journal = true;

		if (!s->update_journal) {
			bch2_cut_front(cur_end, insert);
			bch2_cut_front(cur_end, &s->whiteout);
			bch2_btree_iter_set_pos_same_leaf(iter, cur_end);
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
				_k->type = KEY_TYPE_discard;
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

		extent_squash(c, iter, insert, _k, k, overlap);

		if (!s->update_btree)
			bch2_cut_front(cur_end, insert);
next:
		if (overlap == BCH_EXTENT_OVERLAP_FRONT ||
		    overlap == BCH_EXTENT_OVERLAP_MIDDLE)
			break;
	}

	/*
	 * may have skipped past some deleted extents greater than the insert
	 * key, before we got to a non deleted extent and knew we could bail out
	 * rewind the iterator a bit if necessary:
	 */
	{
		struct btree_node_iter node_iter = l->iter;

		while ((_k = bch2_btree_node_iter_prev_all(&node_iter, l->b)) &&
		       bkey_cmp_left_packed(l->b, _k, &insert->k.p) > 0)
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
void bch2_insert_fixup_extent(struct btree_insert *trans,
			      struct btree_insert_entry *insert)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter	= insert->iter;
	struct extent_insert_state s = {
		.whiteout	= *insert->k,
		.update_journal	= !bkey_whiteout(&insert->k->k),
		.update_btree	= !bkey_whiteout(&insert->k->k),
		.deleting	= bkey_whiteout(&insert->k->k),
	};
	BKEY_PADDED(k) tmp;

	EBUG_ON(iter->level);
	EBUG_ON(!insert->k->k.size);
	EBUG_ON(bkey_cmp(iter->pos, bkey_start_pos(&insert->k->k)));

	__bch2_insert_fixup_extent(c, iter, insert->k, &s);

	bch2_btree_iter_set_pos_same_leaf(iter, insert->k->k.p);

	if (s.update_btree) {
		bkey_copy(&tmp.k, insert->k);

		if (s.deleting)
			tmp.k.k.type = KEY_TYPE_discard;

		if (debug_check_bkeys(c))
			bch2_bkey_debugcheck(c, iter->l[0].b,
					     bkey_i_to_s_c(&tmp.k));

		EBUG_ON(bkey_deleted(&tmp.k.k) || !tmp.k.k.size);

		extent_bset_insert(c, iter, &tmp.k);
	}

	if (s.update_journal) {
		bkey_copy(&tmp.k, !s.deleting ? insert->k : &s.whiteout);

		if (s.deleting)
			tmp.k.k.type = KEY_TYPE_discard;

		EBUG_ON(bkey_deleted(&tmp.k.k) || !tmp.k.k.size);

		bch2_btree_journal_key(trans, iter, &tmp.k);
	}

	bch2_cut_front(insert->k->k.p, insert->k);
}

const char *bch2_extent_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
	const union bch_extent_entry *entry;
	struct bch_extent_crc_unpacked crc;
	const struct bch_extent_ptr *ptr;
	unsigned size_ondisk = e.k->size;
	const char *reason;
	unsigned nonce = UINT_MAX;

	if (bkey_val_u64s(e.k) > BKEY_EXTENT_VAL_U64s_MAX)
		return "value too big";

	extent_for_each_entry(e, entry) {
		if (__extent_entry_type(entry) >= BCH_EXTENT_ENTRY_MAX)
			return "invalid extent entry type";

		switch (extent_entry_type(entry)) {
		case BCH_EXTENT_ENTRY_ptr:
			ptr = entry_to_ptr(entry);

			reason = extent_ptr_invalid(c, e.s_c, &entry->ptr,
						    size_ondisk, false);
			if (reason)
				return reason;
			break;
		case BCH_EXTENT_ENTRY_crc32:
		case BCH_EXTENT_ENTRY_crc64:
		case BCH_EXTENT_ENTRY_crc128:
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
			break;
		case BCH_EXTENT_ENTRY_stripe_ptr:
			break;
		}
	}

	return NULL;
}

void bch2_extent_debugcheck(struct bch_fs *c, struct btree *b,
			    struct bkey_s_c k)
{
	struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	char buf[160];

	/*
	 * XXX: we should be doing most/all of these checks at startup time,
	 * where we check bch2_bkey_invalid() in btree_node_read_done()
	 *
	 * But note that we can't check for stale pointers or incorrect gc marks
	 * until after journal replay is done (it might be an extent that's
	 * going to get overwritten during replay)
	 */

	bch2_fs_bug_on(!test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) &&
		       !bch2_bkey_replicas_marked(c, e.s_c, false), c,
		       "extent key bad (replicas not marked in superblock):\n%s",
		       (bch2_bkey_val_to_text(&PBUF(buf), c, e.s_c), buf));

	/*
	 * If journal replay hasn't finished, we might be seeing keys
	 * that will be overwritten by the time journal replay is done:
	 */
	if (!test_bit(JOURNAL_REPLAY_DONE, &c->journal.flags))
		return;

	extent_for_each_ptr_decode(e, p, entry) {
		struct bch_dev *ca	= bch_dev_bkey_exists(c, p.ptr.dev);
		struct bucket_mark mark = ptr_bucket_mark(ca, &p.ptr);
		unsigned stale		= gen_after(mark.gen, p.ptr.gen);
		unsigned disk_sectors	= ptr_disk_sectors(p);
		unsigned mark_sectors	= p.ptr.cached
			? mark.cached_sectors
			: mark.dirty_sectors;

		bch2_fs_bug_on(stale && !p.ptr.cached, c,
			       "stale dirty pointer (ptr gen %u bucket %u",
			       p.ptr.gen, mark.gen);

		bch2_fs_bug_on(stale > 96, c, "key too stale: %i", stale);

		bch2_fs_bug_on(!stale &&
			       (mark.data_type != BCH_DATA_USER ||
				mark_sectors < disk_sectors), c,
			       "extent pointer not marked: %s:\n"
			       "type %u sectors %u < %u",
			       (bch2_bkey_val_to_text(&PBUF(buf), c, e.s_c), buf),
			       mark.data_type,
			       mark_sectors, disk_sectors);
	}
}

void bch2_extent_to_text(struct printbuf *out, struct bch_fs *c,
			 struct bkey_s_c k)
{
	const char *invalid;

	bkey_ptrs_to_text(out, c, k);

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
	unsigned i;

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

	for (i = 0; i < p->ec_nr; i++) {
		p->ec[i].type = 1 << BCH_EXTENT_ENTRY_stripe_ptr;
		__extent_entry_insert(e, pos, to_entry(&p->ec[i]));
	}
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
	struct bch_extent_ptr *ptr;

	bch2_bkey_drop_ptrs(k, ptr,
		ptr->cached &&
		ptr_stale(bch_dev_bkey_exists(c, ptr->dev), ptr));

	/* will only happen if all pointers were cached: */
	if (!bkey_val_u64s(k.k))
		k.k->type = KEY_TYPE_deleted;

	return false;
}

void bch2_extent_mark_replicas_cached(struct bch_fs *c,
				      struct bkey_s_extent e,
				      unsigned target,
				      unsigned nr_desired_replicas)
{
	union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	int extra = bch2_bkey_durability(c, e.s_c) - nr_desired_replicas;

	if (target && extra > 0)
		extent_for_each_ptr_decode(e, p, entry) {
			int n = bch2_extent_ptr_durability(c, p);

			if (n && n <= extra &&
			    !bch2_dev_in_target(c, p.ptr.dev, target)) {
				entry->ptr.cached = true;
				extra -= n;
			}
		}

	if (extra > 0)
		extent_for_each_ptr_decode(e, p, entry) {
			int n = bch2_extent_ptr_durability(c, p);

			if (n && n <= extra) {
				entry->ptr.cached = true;
				extra -= n;
			}
		}
}

enum merge_result bch2_extent_merge(struct bch_fs *c,
				    struct bkey_i *l, struct bkey_i *r)
{
	struct bkey_s_extent el = bkey_i_to_s_extent(l);
	struct bkey_s_extent er = bkey_i_to_s_extent(r);
	union bch_extent_entry *en_l, *en_r;

	if (bkey_val_u64s(&l->k) != bkey_val_u64s(&r->k))
		return BCH_MERGE_NOMERGE;

	extent_for_each_entry(el, en_l) {
		struct bch_extent_ptr *lp, *rp;
		struct bch_dev *ca;

		en_r = vstruct_idx(er.v, (u64 *) en_l - el.v->_data);

		if ((extent_entry_type(en_l) !=
		     extent_entry_type(en_r)) ||
		    !extent_entry_is_ptr(en_l))
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

	ret = bch2_bkey_merge(c, &li.k, &ri.k);
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

bool bch2_check_range_allocated(struct bch_fs *c, struct bpos pos, u64 size,
			       unsigned nr_replicas)
{
	struct btree_iter iter;
	struct bpos end = pos;
	struct bkey_s_c k;
	bool ret = true;

	end.offset += size;

	for_each_btree_key(&iter, c, BTREE_ID_EXTENTS, pos,
			     BTREE_ITER_SLOTS, k) {
		if (bkey_cmp(bkey_start_pos(k.k), end) >= 0)
			break;

		if (nr_replicas > bch2_bkey_nr_ptrs_allocated(k)) {
			ret = false;
			break;
		}
	}
	bch2_btree_iter_unlock(&iter);

	return ret;
}

unsigned bch2_bkey_nr_ptrs_allocated(struct bkey_s_c k)
{
	unsigned ret = 0;

	switch (k.k->type) {
	case KEY_TYPE_extent: {
		struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;

		extent_for_each_ptr_decode(e, p, entry)
			ret += !p.ptr.cached &&
				p.crc.compression_type == BCH_COMPRESSION_NONE;
		break;
	}
	case KEY_TYPE_reservation:
		ret = bkey_s_c_to_reservation(k).v->nr_replicas;
		break;
	}

	return ret;
}

/* KEY_TYPE_reservation: */

const char *bch2_reservation_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_reservation r = bkey_s_c_to_reservation(k);

	if (bkey_val_bytes(k.k) != sizeof(struct bch_reservation))
		return "incorrect value size";

	if (!r.v->nr_replicas || r.v->nr_replicas > BCH_REPLICAS_MAX)
		return "invalid nr_replicas";

	return NULL;
}

void bch2_reservation_to_text(struct printbuf *out, struct bch_fs *c,
			      struct bkey_s_c k)
{
	struct bkey_s_c_reservation r = bkey_s_c_to_reservation(k);

	pr_buf(out, "generation %u replicas %u",
	       le32_to_cpu(r.v->generation),
	       r.v->nr_replicas);
}

enum merge_result bch2_reservation_merge(struct bch_fs *c,
					 struct bkey_i *l, struct bkey_i *r)
{
	struct bkey_i_reservation *li = bkey_i_to_reservation(l);
	struct bkey_i_reservation *ri = bkey_i_to_reservation(r);

	if (li->v.generation != ri->v.generation ||
	    li->v.nr_replicas != ri->v.nr_replicas)
		return BCH_MERGE_NOMERGE;

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
