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
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v: {
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

const struct bch_extent_ptr *
bch2_bkey_has_device(struct bkey_s_c k, unsigned dev)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;

	bkey_for_each_ptr(ptrs, ptr)
		if (ptr->dev == dev)
			return ptr;

	return NULL;
}

bool bch2_bkey_has_target(struct bch_fs *c, struct bkey_s_c k, unsigned target)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;

	bkey_for_each_ptr(ptrs, ptr)
		if (bch2_dev_in_target(c, ptr->dev, target) &&
		    (!ptr->cached ||
		     !ptr_stale(bch_dev_bkey_exists(c, ptr->dev), ptr)))
			return true;

	return false;
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

unsigned bch2_extent_is_compressed(struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned ret = 0;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
		if (!p.ptr.cached &&
		    p.crc.compression_type != BCH_COMPRESSION_NONE)
			ret += p.crc.compressed_size;

	return ret;
}

bool bch2_bkey_matches_ptr(struct bch_fs *c, struct bkey_s_c k,
			   struct bch_extent_ptr m, u64 offset)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
		if (p.ptr.dev	== m.dev &&
		    p.ptr.gen	== m.gen &&
		    (s64) p.ptr.offset + p.crc.offset - bkey_start_offset(k.k) ==
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

bool bch2_can_narrow_extent_crcs(struct bkey_s_c k,
				 struct bch_extent_crc_unpacked n)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	struct bch_extent_crc_unpacked crc;
	const union bch_extent_entry *i;

	if (!n.csum_type)
		return false;

	bkey_for_each_crc(k.k, ptrs, crc, i)
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
bool bch2_bkey_narrow_crcs(struct bkey_i *k, struct bch_extent_crc_unpacked n)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(bkey_i_to_s(k));
	struct bch_extent_crc_unpacked u;
	struct extent_ptr_decoded p;
	union bch_extent_entry *i;
	bool ret = false;

	/* Find a checksum entry that covers only live data: */
	if (!n.csum_type) {
		bkey_for_each_crc(&k->k, ptrs, u, i)
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
	BUG_ON(n.live_size != k->k.size);

restart_narrow_pointers:
	ptrs = bch2_bkey_ptrs(bkey_i_to_s(k));

	bkey_for_each_ptr_decode(&k->k, ptrs, p, i)
		if (can_narrow_crc(p.crc, n)) {
			bch2_bkey_drop_ptr(bkey_i_to_s(k), &i->ptr);
			p.ptr.offset += p.crc.offset;
			p.crc = n;
			bch2_extent_ptr_decoded_append(k, &p);
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

void bch2_bkey_ptrs_to_text(struct printbuf *out, struct bch_fs *c,
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

static const char *extent_ptr_invalid(const struct bch_fs *c,
				      struct bkey_s_c k,
				      const struct bch_extent_ptr *ptr,
				      unsigned size_ondisk,
				      bool metadata)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr2;
	struct bch_dev *ca;

	if (!bch2_dev_exists2(c, ptr->dev))
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

const char *bch2_bkey_ptrs_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct bch_extent_crc_unpacked crc;
	unsigned size_ondisk = k.k->size;
	const char *reason;
	unsigned nonce = UINT_MAX;

	if (k.k->type == KEY_TYPE_btree_ptr)
		size_ondisk = c->opts.btree_node_size;

	bkey_extent_entry_for_each(ptrs, entry) {
		if (__extent_entry_type(entry) >= BCH_EXTENT_ENTRY_MAX)
			return "invalid extent entry type";

		if (k.k->type == KEY_TYPE_btree_ptr &&
		    !extent_entry_is_ptr(entry))
			return "has non ptr field";

		switch (extent_entry_type(entry)) {
		case BCH_EXTENT_ENTRY_ptr:
			reason = extent_ptr_invalid(c, k, &entry->ptr,
						    size_ondisk, false);
			if (reason)
				return reason;
			break;
		case BCH_EXTENT_ENTRY_crc32:
		case BCH_EXTENT_ENTRY_crc64:
		case BCH_EXTENT_ENTRY_crc128:
			crc = bch2_extent_crc_unpack(k.k, entry_to_crc(entry));

			if (crc.offset + crc.live_size >
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

/* Btree ptrs */

const char *bch2_btree_ptr_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	if (bkey_val_u64s(k.k) > BKEY_BTREE_PTR_VAL_U64s_MAX)
		return "value too big";

	return bch2_bkey_ptrs_invalid(c, k);
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
	bch2_bkey_ptrs_to_text(out, c, k);
}

/* Extents */

void __bch2_cut_front(struct bpos where, struct bkey_s k)
{
	u64 sub;

	if (bkey_cmp(where, bkey_start_pos(k.k)) <= 0)
		return;

	EBUG_ON(bkey_cmp(where, k.k->p) > 0);

	sub = where.offset - bkey_start_offset(k.k);

	k.k->size -= sub;

	if (!k.k->size)
		k.k->type = KEY_TYPE_deleted;

	switch (k.k->type) {
	case KEY_TYPE_deleted:
	case KEY_TYPE_discard:
	case KEY_TYPE_error:
	case KEY_TYPE_cookie:
		break;
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v: {
		struct bkey_ptrs ptrs = bch2_bkey_ptrs(k);
		union bch_extent_entry *entry;
		bool seen_crc = false;

		bkey_extent_entry_for_each(ptrs, entry) {
			switch (extent_entry_type(entry)) {
			case BCH_EXTENT_ENTRY_ptr:
				if (!seen_crc)
					entry->ptr.offset += sub;
				break;
			case BCH_EXTENT_ENTRY_crc32:
				entry->crc32.offset += sub;
				break;
			case BCH_EXTENT_ENTRY_crc64:
				entry->crc64.offset += sub;
				break;
			case BCH_EXTENT_ENTRY_crc128:
				entry->crc128.offset += sub;
				break;
			case BCH_EXTENT_ENTRY_stripe_ptr:
				break;
			}

			if (extent_entry_is_crc(entry))
				seen_crc = true;
		}

		break;
	}
	case KEY_TYPE_reflink_p: {
		struct bkey_s_reflink_p p = bkey_s_to_reflink_p(k);

		le64_add_cpu(&p.v->idx, sub);
		break;
	}
	case KEY_TYPE_reservation:
		break;
	default:
		BUG();
	}
}

bool bch2_cut_back(struct bpos where, struct bkey *k)
{
	u64 len = 0;

	if (bkey_cmp(where, k->p) >= 0)
		return false;

	EBUG_ON(bkey_cmp(where, bkey_start_pos(k)) < 0);

	len = where.offset - bkey_start_offset(k);

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

static void verify_extent_nonoverlapping(struct bch_fs *c,
					 struct btree *b,
					 struct btree_node_iter *_iter,
					 struct bkey_i *insert)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct btree_node_iter iter;
	struct bkey_packed *k;
	struct bkey uk;

	if (!expensive_debug_checks(c))
		return;

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
	verify_extent_nonoverlapping(c, l->b, &l->iter, insert);

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

	/*
	 * may have skipped past some deleted extents greater than the insert
	 * key, before we got to a non deleted extent and knew we could bail out
	 * rewind the iterator a bit if necessary:
	 */
	node_iter = l->iter;
	while ((k = bch2_btree_node_iter_prev_all(&node_iter, l->b)) &&
	       bkey_cmp_left_packed(l->b, k, &insert->k.p) > 0)
		l->iter = node_iter;

	k = bch2_btree_node_iter_bset_pos(&l->iter, l->b, bset_tree_last(l->b));

	bch2_bset_insert(l->b, &l->iter, k, insert, 0);
	bch2_btree_node_iter_fix(iter, l->b, &l->iter, k, 0, k->u64s);
	bch2_btree_iter_verify(iter, l->b);
}

static unsigned bch2_bkey_nr_alloc_ptrs(struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	unsigned ret = 0;

	bkey_extent_entry_for_each(ptrs, entry) {
		switch (__extent_entry_type(entry)) {
		case BCH_EXTENT_ENTRY_ptr:
		case BCH_EXTENT_ENTRY_stripe_ptr:
			ret++;
		}
	}

	return ret;
}

static int __bch2_extent_atomic_end(struct btree_trans *trans,
				    struct bkey_s_c k,
				    unsigned offset,
				    struct bpos *end,
				    unsigned *nr_iters,
				    unsigned max_iters)
{
	int ret = 0;

	switch (k.k->type) {
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		*nr_iters += bch2_bkey_nr_alloc_ptrs(k);

		if (*nr_iters >= max_iters) {
			*end = bpos_min(*end, k.k->p);
			return 0;
		}

		break;
	case KEY_TYPE_reflink_p: {
		struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);
		u64 idx = le64_to_cpu(p.v->idx);
		unsigned sectors = end->offset - bkey_start_offset(p.k);
		struct btree_iter *iter;
		struct bkey_s_c r_k;

		for_each_btree_key(trans, iter,
				   BTREE_ID_REFLINK, POS(0, idx + offset),
				   BTREE_ITER_SLOTS, r_k, ret) {
			if (bkey_cmp(bkey_start_pos(r_k.k),
				     POS(0, idx + sectors)) >= 0)
				break;

			*nr_iters += 1;
			if (*nr_iters >= max_iters) {
				struct bpos pos = bkey_start_pos(k.k);
				pos.offset += r_k.k->p.offset - idx;

				*end = bpos_min(*end, pos);
				break;
			}
		}

		bch2_trans_iter_put(trans, iter);
		break;
	}
	}

	return ret;
}

int bch2_extent_atomic_end(struct btree_trans *trans,
			   struct btree_iter *iter,
			   struct bkey_i *insert,
			   struct bpos *end)
{
	struct btree *b = iter->l[0].b;
	struct btree_node_iter	node_iter = iter->l[0].iter;
	struct bkey_packed	*_k;
	unsigned		nr_iters =
		bch2_bkey_nr_alloc_ptrs(bkey_i_to_s_c(insert));
	int ret = 0;

	BUG_ON(iter->uptodate > BTREE_ITER_NEED_PEEK);
	BUG_ON(bkey_cmp(bkey_start_pos(&insert->k), b->data->min_key) < 0);

	*end = bpos_min(insert->k.p, b->key.k.p);

	ret = __bch2_extent_atomic_end(trans, bkey_i_to_s_c(insert),
				       0, end, &nr_iters, 10);
	if (ret)
		return ret;

	while (nr_iters < 20 &&
	       (_k = bch2_btree_node_iter_peek_filter(&node_iter, b,
						      KEY_TYPE_discard))) {
		struct bkey	unpacked;
		struct bkey_s_c	k = bkey_disassemble(b, _k, &unpacked);
		unsigned offset = 0;

		if (bkey_cmp(bkey_start_pos(k.k), *end) >= 0)
			break;

		if (bkey_cmp(bkey_start_pos(&insert->k),
			     bkey_start_pos(k.k)) > 0)
			offset = bkey_start_offset(&insert->k) -
				bkey_start_offset(k.k);

		ret = __bch2_extent_atomic_end(trans, k, offset,
					       end, &nr_iters, 20);
		if (ret)
			return ret;

		if (nr_iters >= 20)
			break;

		bch2_btree_node_iter_advance(&node_iter, b);
	}

	return 0;
}

int bch2_extent_trim_atomic(struct bkey_i *k, struct btree_iter *iter)
{
	struct bpos end;
	int ret;

	ret = bch2_extent_atomic_end(iter->trans, iter, k, &end);
	if (ret)
		return ret;

	bch2_cut_back(end, &k->k);
	return 0;
}

int bch2_extent_is_atomic(struct bkey_i *k, struct btree_iter *iter)
{
	struct bpos end;
	int ret;

	ret = bch2_extent_atomic_end(iter->trans, iter, k, &end);
	if (ret)
		return ret;

	return !bkey_cmp(end, k->k.p);
}

enum btree_insert_ret
bch2_extent_can_insert(struct btree_trans *trans,
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
void bch2_insert_fixup_extent(struct btree_trans *trans,
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
	return bch2_bkey_ptrs_invalid(c, k);
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

	if (percpu_down_read_trylock(&c->mark_lock)) {
		bch2_fs_bug_on(!test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) &&
			       !bch2_bkey_replicas_marked_locked(c, e.s_c, false), c,
			       "extent key bad (replicas not marked in superblock):\n%s",
			       (bch2_bkey_val_to_text(&PBUF(buf), c, e.s_c), buf));
		percpu_up_read(&c->mark_lock);
	}
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
	bch2_bkey_ptrs_to_text(out, c, k);
}

static unsigned bch2_crc_field_size_max[] = {
	[BCH_EXTENT_ENTRY_crc32] = CRC32_SIZE_MAX,
	[BCH_EXTENT_ENTRY_crc64] = CRC64_SIZE_MAX,
	[BCH_EXTENT_ENTRY_crc128] = CRC128_SIZE_MAX,
};

static void bch2_extent_crc_pack(union bch_extent_crc *dst,
				 struct bch_extent_crc_unpacked src)
{
#define set_common_fields(_dst, _src)					\
		_dst.csum_type		= _src.csum_type,		\
		_dst.compression_type	= _src.compression_type,	\
		_dst._compressed_size	= _src.compressed_size - 1,	\
		_dst._uncompressed_size	= _src.uncompressed_size - 1,	\
		_dst.offset		= _src.offset

	switch (extent_entry_type(to_entry(dst))) {
	case BCH_EXTENT_ENTRY_crc32:
		set_common_fields(dst->crc32, src);
		dst->crc32.csum	 = *((__le32 *) &src.csum.lo);
		break;
	case BCH_EXTENT_ENTRY_crc64:
		set_common_fields(dst->crc64, src);
		dst->crc64.nonce	= src.nonce;
		dst->crc64.csum_lo	= src.csum.lo;
		dst->crc64.csum_hi	= *((__le16 *) &src.csum.hi);
		break;
	case BCH_EXTENT_ENTRY_crc128:
		set_common_fields(dst->crc128, src);
		dst->crc128.nonce	= src.nonce;
		dst->crc128.csum	= src.csum;
		break;
	default:
		BUG();
	}
#undef set_common_fields
}

void bch2_extent_crc_append(struct bkey_i *k,
			    struct bch_extent_crc_unpacked new)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(bkey_i_to_s(k));
	union bch_extent_crc *crc = (void *) ptrs.end;

	if (bch_crc_bytes[new.csum_type]	<= 4 &&
	    new.uncompressed_size - 1		<= CRC32_SIZE_MAX &&
	    new.nonce				<= CRC32_NONCE_MAX)
		crc->type = 1 << BCH_EXTENT_ENTRY_crc32;
	else if (bch_crc_bytes[new.csum_type]	<= 10 &&
		   new.uncompressed_size - 1	<= CRC64_SIZE_MAX &&
		   new.nonce			<= CRC64_NONCE_MAX)
		crc->type = 1 << BCH_EXTENT_ENTRY_crc64;
	else if (bch_crc_bytes[new.csum_type]	<= 16 &&
		   new.uncompressed_size - 1	<= CRC128_SIZE_MAX &&
		   new.nonce			<= CRC128_NONCE_MAX)
		crc->type = 1 << BCH_EXTENT_ENTRY_crc128;
	else
		BUG();

	bch2_extent_crc_pack(crc, new);

	k->k.u64s += extent_entry_u64s(ptrs.end);

	EBUG_ON(bkey_val_u64s(&k->k) > BKEY_EXTENT_VAL_U64s_MAX);
}

static inline void __extent_entry_insert(struct bkey_i *k,
					 union bch_extent_entry *dst,
					 union bch_extent_entry *new)
{
	union bch_extent_entry *end = bkey_val_end(bkey_i_to_s(k));

	memmove_u64s_up((u64 *) dst + extent_entry_u64s(new),
			dst, (u64 *) end - (u64 *) dst);
	k->k.u64s += extent_entry_u64s(new);
	memcpy_u64s_small(dst, new, extent_entry_u64s(new));
}

void bch2_extent_ptr_decoded_append(struct bkey_i *k,
				    struct extent_ptr_decoded *p)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(bkey_i_to_s(k));
	struct bch_extent_crc_unpacked crc =
		bch2_extent_crc_unpack(&k->k, NULL);
	union bch_extent_entry *pos;
	unsigned i;

	if (!bch2_crc_unpacked_cmp(crc, p->crc)) {
		pos = ptrs.start;
		goto found;
	}

	bkey_for_each_crc(&k->k, ptrs, crc, pos)
		if (!bch2_crc_unpacked_cmp(crc, p->crc)) {
			pos = extent_entry_next(pos);
			goto found;
		}

	bch2_extent_crc_append(k, p->crc);
	pos = bkey_val_end(bkey_i_to_s(k));
found:
	p->ptr.type = 1 << BCH_EXTENT_ENTRY_ptr;
	__extent_entry_insert(k, pos, to_entry(&p->ptr));

	for (i = 0; i < p->ec_nr; i++) {
		p->ec[i].type = 1 << BCH_EXTENT_ENTRY_stripe_ptr;
		__extent_entry_insert(k, pos, to_entry(&p->ec[i]));
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
		k.k->type = KEY_TYPE_discard;

	return bkey_whiteout(k.k);
}

void bch2_bkey_mark_replicas_cached(struct bch_fs *c, struct bkey_s k,
				    unsigned target,
				    unsigned nr_desired_replicas)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(k);
	union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	int extra = bch2_bkey_durability(c, k.s_c) - nr_desired_replicas;

	if (target && extra > 0)
		bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
			int n = bch2_extent_ptr_durability(c, p);

			if (n && n <= extra &&
			    !bch2_dev_in_target(c, p.ptr.dev, target)) {
				entry->ptr.cached = true;
				extra -= n;
			}
		}

	if (extra > 0)
		bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
			int n = bch2_extent_ptr_durability(c, p);

			if (n && n <= extra) {
				entry->ptr.cached = true;
				extra -= n;
			}
		}
}

enum merge_result bch2_extent_merge(struct bch_fs *c,
				    struct bkey_s _l, struct bkey_s _r)
{
	struct bkey_s_extent l = bkey_s_to_extent(_l);
	struct bkey_s_extent r = bkey_s_to_extent(_r);
	union bch_extent_entry *en_l = l.v->start;
	union bch_extent_entry *en_r = r.v->start;
	struct bch_extent_crc_unpacked crc_l, crc_r;

	if (bkey_val_u64s(l.k) != bkey_val_u64s(r.k))
		return BCH_MERGE_NOMERGE;

	crc_l = bch2_extent_crc_unpack(l.k, NULL);

	extent_for_each_entry(l, en_l) {
		en_r = vstruct_idx(r.v, (u64 *) en_l - l.v->_data);

		if (extent_entry_type(en_l) != extent_entry_type(en_r))
			return BCH_MERGE_NOMERGE;

		switch (extent_entry_type(en_l)) {
		case BCH_EXTENT_ENTRY_ptr: {
			const struct bch_extent_ptr *lp = &en_l->ptr;
			const struct bch_extent_ptr *rp = &en_r->ptr;
			struct bch_dev *ca;

			if (lp->offset + crc_l.compressed_size != rp->offset ||
			    lp->dev			!= rp->dev ||
			    lp->gen			!= rp->gen)
				return BCH_MERGE_NOMERGE;

			/* We don't allow extents to straddle buckets: */
			ca = bch_dev_bkey_exists(c, lp->dev);

			if (PTR_BUCKET_NR(ca, lp) != PTR_BUCKET_NR(ca, rp))
				return BCH_MERGE_NOMERGE;

			break;
		}
		case BCH_EXTENT_ENTRY_stripe_ptr:
			if (en_l->stripe_ptr.block	!= en_r->stripe_ptr.block ||
			    en_l->stripe_ptr.idx	!= en_r->stripe_ptr.idx)
				return BCH_MERGE_NOMERGE;
			break;
		case BCH_EXTENT_ENTRY_crc32:
		case BCH_EXTENT_ENTRY_crc64:
		case BCH_EXTENT_ENTRY_crc128:
			crc_l = bch2_extent_crc_unpack(l.k, entry_to_crc(en_l));
			crc_r = bch2_extent_crc_unpack(r.k, entry_to_crc(en_r));

			if (crc_l.csum_type		!= crc_r.csum_type ||
			    crc_l.compression_type	!= crc_r.compression_type ||
			    crc_l.nonce			!= crc_r.nonce)
				return BCH_MERGE_NOMERGE;

			if (crc_l.offset + crc_l.live_size != crc_l.compressed_size ||
			    crc_r.offset)
				return BCH_MERGE_NOMERGE;

			if (!bch2_checksum_mergeable(crc_l.csum_type))
				return BCH_MERGE_NOMERGE;

			if (crc_l.compression_type)
				return BCH_MERGE_NOMERGE;

			if (crc_l.csum_type &&
			    crc_l.uncompressed_size +
			    crc_r.uncompressed_size > c->sb.encoded_extent_max)
				return BCH_MERGE_NOMERGE;

			if (crc_l.uncompressed_size + crc_r.uncompressed_size - 1 >
			    bch2_crc_field_size_max[extent_entry_type(en_l)])
				return BCH_MERGE_NOMERGE;

			break;
		default:
			return BCH_MERGE_NOMERGE;
		}
	}

	extent_for_each_entry(l, en_l) {
		struct bch_extent_crc_unpacked crc_l, crc_r;

		en_r = vstruct_idx(r.v, (u64 *) en_l - l.v->_data);

		if (!extent_entry_is_crc(en_l))
			continue;

		crc_l = bch2_extent_crc_unpack(l.k, entry_to_crc(en_l));
		crc_r = bch2_extent_crc_unpack(r.k, entry_to_crc(en_r));

		crc_l.csum = bch2_checksum_merge(crc_l.csum_type,
						 crc_l.csum,
						 crc_r.csum,
						 crc_r.uncompressed_size << 9);

		crc_l.uncompressed_size	+= crc_r.uncompressed_size;
		crc_l.compressed_size	+= crc_r.compressed_size;

		bch2_extent_crc_pack(entry_to_crc(en_l), crc_l);
	}

	bch2_key_resize(l.k, l.k->size + r.k->size);

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

	if (bkey_val_u64s(l) > BKEY_EXTENT_VAL_U64s_MAX ||
	    bkey_val_u64s(r) > BKEY_EXTENT_VAL_U64s_MAX)
		return BCH_MERGE_NOMERGE;

	/*
	 * We need to save copies of both l and r, because we might get a
	 * partial merge (which modifies both) and then fails to repack
	 */
	bch2_bkey_unpack(b, &li.k, l);
	bch2_bkey_unpack(b, &ri.k, r);

	ret = bch2_bkey_merge(c,
			      bkey_i_to_s(&li.k),
			      bkey_i_to_s(&ri.k));
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
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bpos end = pos;
	struct bkey_s_c k;
	bool ret = true;
	int err;

	end.offset += size;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS, pos,
			   BTREE_ITER_SLOTS, k, err) {
		if (bkey_cmp(bkey_start_pos(k.k), end) >= 0)
			break;

		if (nr_replicas > bch2_bkey_nr_ptrs_allocated(k)) {
			ret = false;
			break;
		}
	}
	bch2_trans_exit(&trans);

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
					 struct bkey_s _l, struct bkey_s _r)
{
	struct bkey_s_reservation l = bkey_s_to_reservation(_l);
	struct bkey_s_reservation r = bkey_s_to_reservation(_r);

	if (l.v->generation != r.v->generation ||
	    l.v->nr_replicas != r.v->nr_replicas)
		return BCH_MERGE_NOMERGE;

	if ((u64) l.k->size + r.k->size > KEY_SIZE_MAX) {
		bch2_key_resize(l.k, KEY_SIZE_MAX);
		__bch2_cut_front(l.k->p, r.s);
		return BCH_MERGE_PARTIAL;
	}

	bch2_key_resize(l.k, l.k->size + r.k->size);

	return BCH_MERGE_MERGE;
}
