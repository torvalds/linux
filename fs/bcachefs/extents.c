// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Kent Overstreet <kent.overstreet@gmail.com>
 *
 * Code for managing the extent btree and dynamically updating the writeback
 * dirty sector count.
 */

#include "bcachefs.h"
#include "bkey_methods.h"
#include "btree_cache.h"
#include "btree_gc.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "buckets.h"
#include "checksum.h"
#include "compress.h"
#include "debug.h"
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

static unsigned bch2_crc_field_size_max[] = {
	[BCH_EXTENT_ENTRY_crc32] = CRC32_SIZE_MAX,
	[BCH_EXTENT_ENTRY_crc64] = CRC64_SIZE_MAX,
	[BCH_EXTENT_ENTRY_crc128] = CRC128_SIZE_MAX,
};

static void bch2_extent_crc_pack(union bch_extent_crc *,
				 struct bch_extent_crc_unpacked,
				 enum bch_extent_entry_type);

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

static inline u64 dev_latency(struct bch_fs *c, unsigned dev)
{
	struct bch_dev *ca = bch2_dev_rcu(c, dev);
	return ca ? atomic64_read(&ca->cur_latency[READ]) : S64_MAX;
}

/*
 * returns true if p1 is better than p2:
 */
static inline bool ptr_better(struct bch_fs *c,
			      const struct extent_ptr_decoded p1,
			      const struct extent_ptr_decoded p2)
{
	if (likely(!p1.idx && !p2.idx)) {
		u64 l1 = dev_latency(c, p1.ptr.dev);
		u64 l2 = dev_latency(c, p2.ptr.dev);

		/* Pick at random, biased in favor of the faster device: */

		return bch2_rand_range(l1 + l2) > l1;
	}

	if (bch2_force_reconstruct_read)
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
	int ret = 0;

	if (k.k->type == KEY_TYPE_error)
		return -EIO;

	rcu_read_lock();
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		/*
		 * Unwritten extent: no need to actually read, treat it as a
		 * hole and return 0s:
		 */
		if (p.ptr.unwritten) {
			ret = 0;
			break;
		}

		/*
		 * If there are any dirty pointers it's an error if we can't
		 * read:
		 */
		if (!ret && !p.ptr.cached)
			ret = -EIO;

		struct bch_dev *ca = bch2_dev_rcu(c, p.ptr.dev);

		if (p.ptr.cached && (!ca || dev_ptr_stale(ca, &p.ptr)))
			continue;

		f = failed ? dev_io_failures(failed, p.ptr.dev) : NULL;
		if (f)
			p.idx = f->nr_failed < f->nr_retries
				? f->idx
				: f->idx + 1;

		if (!p.idx && !ca)
			p.idx++;

		if (!p.idx && p.has_ec && bch2_force_reconstruct_read)
			p.idx++;

		if (!p.idx && !bch2_dev_is_readable(ca))
			p.idx++;

		if (p.idx >= (unsigned) p.has_ec + 1)
			continue;

		if (ret > 0 && !ptr_better(c, p, *pick))
			continue;

		*pick = p;
		ret = 1;
	}
	rcu_read_unlock();

	return ret;
}

/* KEY_TYPE_btree_ptr: */

int bch2_btree_ptr_invalid(struct bch_fs *c, struct bkey_s_c k,
			   enum bch_validate_flags flags,
			   struct printbuf *err)
{
	int ret = 0;

	bkey_fsck_err_on(bkey_val_u64s(k.k) > BCH_REPLICAS_MAX, c, err,
			 btree_ptr_val_too_big,
			 "value too big (%zu > %u)", bkey_val_u64s(k.k), BCH_REPLICAS_MAX);

	ret = bch2_bkey_ptrs_invalid(c, k, flags, err);
fsck_err:
	return ret;
}

void bch2_btree_ptr_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	bch2_bkey_ptrs_to_text(out, c, k);
}

int bch2_btree_ptr_v2_invalid(struct bch_fs *c, struct bkey_s_c k,
			      enum bch_validate_flags flags,
			      struct printbuf *err)
{
	struct bkey_s_c_btree_ptr_v2 bp = bkey_s_c_to_btree_ptr_v2(k);
	int ret = 0;

	bkey_fsck_err_on(bkey_val_u64s(k.k) > BKEY_BTREE_PTR_VAL_U64s_MAX,
			 c, err, btree_ptr_v2_val_too_big,
			 "value too big (%zu > %zu)",
			 bkey_val_u64s(k.k), BKEY_BTREE_PTR_VAL_U64s_MAX);

	bkey_fsck_err_on(bpos_ge(bp.v->min_key, bp.k->p),
			 c, err, btree_ptr_v2_min_key_bad,
			 "min_key > key");

	if (flags & BCH_VALIDATE_write)
		bkey_fsck_err_on(!bp.v->sectors_written,
				 c, err, btree_ptr_v2_written_0,
				 "sectors_written == 0");

	ret = bch2_bkey_ptrs_invalid(c, k, flags, err);
fsck_err:
	return ret;
}

void bch2_btree_ptr_v2_to_text(struct printbuf *out, struct bch_fs *c,
			       struct bkey_s_c k)
{
	struct bkey_s_c_btree_ptr_v2 bp = bkey_s_c_to_btree_ptr_v2(k);

	prt_printf(out, "seq %llx written %u min_key %s",
	       le64_to_cpu(bp.v->seq),
	       le16_to_cpu(bp.v->sectors_written),
	       BTREE_PTR_RANGE_UPDATED(bp.v) ? "R " : "");

	bch2_bpos_to_text(out, bp.v->min_key);
	prt_printf(out, " ");
	bch2_bkey_ptrs_to_text(out, c, k);
}

void bch2_btree_ptr_v2_compat(enum btree_id btree_id, unsigned version,
			      unsigned big_endian, int write,
			      struct bkey_s k)
{
	struct bkey_s_btree_ptr_v2 bp = bkey_s_to_btree_ptr_v2(k);

	compat_bpos(0, btree_id, version, big_endian, write, &bp.v->min_key);

	if (version < bcachefs_metadata_version_inode_btree_change &&
	    btree_id_is_extents(btree_id) &&
	    !bkey_eq(bp.v->min_key, POS_MIN))
		bp.v->min_key = write
			? bpos_nosnap_predecessor(bp.v->min_key)
			: bpos_nosnap_successor(bp.v->min_key);
}

/* KEY_TYPE_extent: */

bool bch2_extent_merge(struct bch_fs *c, struct bkey_s l, struct bkey_s_c r)
{
	struct bkey_ptrs   l_ptrs = bch2_bkey_ptrs(l);
	struct bkey_ptrs_c r_ptrs = bch2_bkey_ptrs_c(r);
	union bch_extent_entry *en_l;
	const union bch_extent_entry *en_r;
	struct extent_ptr_decoded lp, rp;
	bool use_right_ptr;

	en_l = l_ptrs.start;
	en_r = r_ptrs.start;
	while (en_l < l_ptrs.end && en_r < r_ptrs.end) {
		if (extent_entry_type(en_l) != extent_entry_type(en_r))
			return false;

		en_l = extent_entry_next(en_l);
		en_r = extent_entry_next(en_r);
	}

	if (en_l < l_ptrs.end || en_r < r_ptrs.end)
		return false;

	en_l = l_ptrs.start;
	en_r = r_ptrs.start;
	lp.crc = bch2_extent_crc_unpack(l.k, NULL);
	rp.crc = bch2_extent_crc_unpack(r.k, NULL);

	while (__bkey_ptr_next_decode(l.k, l_ptrs.end, lp, en_l) &&
	       __bkey_ptr_next_decode(r.k, r_ptrs.end, rp, en_r)) {
		if (lp.ptr.offset + lp.crc.offset + lp.crc.live_size !=
		    rp.ptr.offset + rp.crc.offset ||
		    lp.ptr.dev			!= rp.ptr.dev ||
		    lp.ptr.gen			!= rp.ptr.gen ||
		    lp.ptr.unwritten		!= rp.ptr.unwritten ||
		    lp.has_ec			!= rp.has_ec)
			return false;

		/* Extents may not straddle buckets: */
		rcu_read_lock();
		struct bch_dev *ca = bch2_dev_rcu(c, lp.ptr.dev);
		bool same_bucket = ca && PTR_BUCKET_NR(ca, &lp.ptr) == PTR_BUCKET_NR(ca, &rp.ptr);
		rcu_read_unlock();

		if (!same_bucket)
			return false;

		if (lp.has_ec			!= rp.has_ec ||
		    (lp.has_ec &&
		     (lp.ec.block		!= rp.ec.block ||
		      lp.ec.redundancy		!= rp.ec.redundancy ||
		      lp.ec.idx			!= rp.ec.idx)))
			return false;

		if (lp.crc.compression_type	!= rp.crc.compression_type ||
		    lp.crc.nonce		!= rp.crc.nonce)
			return false;

		if (lp.crc.offset + lp.crc.live_size + rp.crc.live_size <=
		    lp.crc.uncompressed_size) {
			/* can use left extent's crc entry */
		} else if (lp.crc.live_size <= rp.crc.offset) {
			/* can use right extent's crc entry */
		} else {
			/* check if checksums can be merged: */
			if (lp.crc.csum_type		!= rp.crc.csum_type ||
			    lp.crc.nonce		!= rp.crc.nonce ||
			    crc_is_compressed(lp.crc) ||
			    !bch2_checksum_mergeable(lp.crc.csum_type))
				return false;

			if (lp.crc.offset + lp.crc.live_size != lp.crc.compressed_size ||
			    rp.crc.offset)
				return false;

			if (lp.crc.csum_type &&
			    lp.crc.uncompressed_size +
			    rp.crc.uncompressed_size > (c->opts.encoded_extent_max >> 9))
				return false;
		}

		en_l = extent_entry_next(en_l);
		en_r = extent_entry_next(en_r);
	}

	en_l = l_ptrs.start;
	en_r = r_ptrs.start;
	while (en_l < l_ptrs.end && en_r < r_ptrs.end) {
		if (extent_entry_is_crc(en_l)) {
			struct bch_extent_crc_unpacked crc_l = bch2_extent_crc_unpack(l.k, entry_to_crc(en_l));
			struct bch_extent_crc_unpacked crc_r = bch2_extent_crc_unpack(r.k, entry_to_crc(en_r));

			if (crc_l.uncompressed_size + crc_r.uncompressed_size >
			    bch2_crc_field_size_max[extent_entry_type(en_l)])
				return false;
		}

		en_l = extent_entry_next(en_l);
		en_r = extent_entry_next(en_r);
	}

	use_right_ptr = false;
	en_l = l_ptrs.start;
	en_r = r_ptrs.start;
	while (en_l < l_ptrs.end) {
		if (extent_entry_type(en_l) == BCH_EXTENT_ENTRY_ptr &&
		    use_right_ptr)
			en_l->ptr = en_r->ptr;

		if (extent_entry_is_crc(en_l)) {
			struct bch_extent_crc_unpacked crc_l =
				bch2_extent_crc_unpack(l.k, entry_to_crc(en_l));
			struct bch_extent_crc_unpacked crc_r =
				bch2_extent_crc_unpack(r.k, entry_to_crc(en_r));

			use_right_ptr = false;

			if (crc_l.offset + crc_l.live_size + crc_r.live_size <=
			    crc_l.uncompressed_size) {
				/* can use left extent's crc entry */
			} else if (crc_l.live_size <= crc_r.offset) {
				/* can use right extent's crc entry */
				crc_r.offset -= crc_l.live_size;
				bch2_extent_crc_pack(entry_to_crc(en_l), crc_r,
						     extent_entry_type(en_l));
				use_right_ptr = true;
			} else {
				crc_l.csum = bch2_checksum_merge(crc_l.csum_type,
								 crc_l.csum,
								 crc_r.csum,
								 crc_r.uncompressed_size << 9);

				crc_l.uncompressed_size	+= crc_r.uncompressed_size;
				crc_l.compressed_size	+= crc_r.compressed_size;
				bch2_extent_crc_pack(entry_to_crc(en_l), crc_l,
						     extent_entry_type(en_l));
			}
		}

		en_l = extent_entry_next(en_l);
		en_r = extent_entry_next(en_r);
	}

	bch2_key_resize(l.k, l.k->size + r.k->size);
	return true;
}

/* KEY_TYPE_reservation: */

int bch2_reservation_invalid(struct bch_fs *c, struct bkey_s_c k,
			     enum bch_validate_flags flags,
			     struct printbuf *err)
{
	struct bkey_s_c_reservation r = bkey_s_c_to_reservation(k);
	int ret = 0;

	bkey_fsck_err_on(!r.v->nr_replicas || r.v->nr_replicas > BCH_REPLICAS_MAX, c, err,
			 reservation_key_nr_replicas_invalid,
			 "invalid nr_replicas (%u)", r.v->nr_replicas);
fsck_err:
	return ret;
}

void bch2_reservation_to_text(struct printbuf *out, struct bch_fs *c,
			      struct bkey_s_c k)
{
	struct bkey_s_c_reservation r = bkey_s_c_to_reservation(k);

	prt_printf(out, "generation %u replicas %u",
	       le32_to_cpu(r.v->generation),
	       r.v->nr_replicas);
}

bool bch2_reservation_merge(struct bch_fs *c, struct bkey_s _l, struct bkey_s_c _r)
{
	struct bkey_s_reservation l = bkey_s_to_reservation(_l);
	struct bkey_s_c_reservation r = bkey_s_c_to_reservation(_r);

	if (l.v->generation != r.v->generation ||
	    l.v->nr_replicas != r.v->nr_replicas)
		return false;

	bch2_key_resize(l.k, l.k->size + r.k->size);
	return true;
}

/* Extent checksum entries: */

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

static inline bool can_narrow_crc(struct bch_extent_crc_unpacked u,
				  struct bch_extent_crc_unpacked n)
{
	return !crc_is_compressed(u) &&
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
			if (!crc_is_compressed(u) &&
			    u.csum_type &&
			    u.live_size == u.uncompressed_size) {
				n = u;
				goto found;
			}
		return false;
	}
found:
	BUG_ON(crc_is_compressed(n));
	BUG_ON(n.offset);
	BUG_ON(n.live_size != k->k.size);

restart_narrow_pointers:
	ptrs = bch2_bkey_ptrs(bkey_i_to_s(k));

	bkey_for_each_ptr_decode(&k->k, ptrs, p, i)
		if (can_narrow_crc(p.crc, n)) {
			bch2_bkey_drop_ptr_noerror(bkey_i_to_s(k), &i->ptr);
			p.ptr.offset += p.crc.offset;
			p.crc = n;
			bch2_extent_ptr_decoded_append(k, &p);
			ret = true;
			goto restart_narrow_pointers;
		}

	return ret;
}

static void bch2_extent_crc_pack(union bch_extent_crc *dst,
				 struct bch_extent_crc_unpacked src,
				 enum bch_extent_entry_type type)
{
#define set_common_fields(_dst, _src)					\
		_dst.type		= 1 << type;			\
		_dst.csum_type		= _src.csum_type,		\
		_dst.compression_type	= _src.compression_type,	\
		_dst._compressed_size	= _src.compressed_size - 1,	\
		_dst._uncompressed_size	= _src.uncompressed_size - 1,	\
		_dst.offset		= _src.offset

	switch (type) {
	case BCH_EXTENT_ENTRY_crc32:
		set_common_fields(dst->crc32, src);
		dst->crc32.csum		= (u32 __force) *((__le32 *) &src.csum.lo);
		break;
	case BCH_EXTENT_ENTRY_crc64:
		set_common_fields(dst->crc64, src);
		dst->crc64.nonce	= src.nonce;
		dst->crc64.csum_lo	= (u64 __force) src.csum.lo;
		dst->crc64.csum_hi	= (u64 __force) *((__le16 *) &src.csum.hi);
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
	enum bch_extent_entry_type type;

	if (bch_crc_bytes[new.csum_type]	<= 4 &&
	    new.uncompressed_size		<= CRC32_SIZE_MAX &&
	    new.nonce				<= CRC32_NONCE_MAX)
		type = BCH_EXTENT_ENTRY_crc32;
	else if (bch_crc_bytes[new.csum_type]	<= 10 &&
		   new.uncompressed_size	<= CRC64_SIZE_MAX &&
		   new.nonce			<= CRC64_NONCE_MAX)
		type = BCH_EXTENT_ENTRY_crc64;
	else if (bch_crc_bytes[new.csum_type]	<= 16 &&
		   new.uncompressed_size	<= CRC128_SIZE_MAX &&
		   new.nonce			<= CRC128_NONCE_MAX)
		type = BCH_EXTENT_ENTRY_crc128;
	else
		BUG();

	bch2_extent_crc_pack(crc, new, type);

	k->k.u64s += extent_entry_u64s(ptrs.end);

	EBUG_ON(bkey_val_u64s(&k->k) > BKEY_EXTENT_VAL_U64s_MAX);
}

/* Generic code for keys with pointers: */

unsigned bch2_bkey_nr_ptrs(struct bkey_s_c k)
{
	return bch2_bkey_devs(k).nr;
}

unsigned bch2_bkey_nr_ptrs_allocated(struct bkey_s_c k)
{
	return k.k->type == KEY_TYPE_reservation
		? bkey_s_c_to_reservation(k).v->nr_replicas
		: bch2_bkey_dirty_devs(k).nr;
}

unsigned bch2_bkey_nr_ptrs_fully_allocated(struct bkey_s_c k)
{
	unsigned ret = 0;

	if (k.k->type == KEY_TYPE_reservation) {
		ret = bkey_s_c_to_reservation(k).v->nr_replicas;
	} else {
		struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;

		bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
			ret += !p.ptr.cached && !crc_is_compressed(p.crc);
	}

	return ret;
}

unsigned bch2_bkey_sectors_compressed(struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned ret = 0;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
		if (!p.ptr.cached && crc_is_compressed(p.crc))
			ret += p.crc.compressed_size;

	return ret;
}

bool bch2_bkey_is_incompressible(struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct bch_extent_crc_unpacked crc;

	bkey_for_each_crc(k.k, ptrs, crc, entry)
		if (crc.compression_type == BCH_COMPRESSION_TYPE_incompressible)
			return true;
	return false;
}

unsigned bch2_bkey_replicas(struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p = { 0 };
	unsigned replicas = 0;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		if (p.ptr.cached)
			continue;

		if (p.has_ec)
			replicas += p.ec.redundancy;

		replicas++;

	}

	return replicas;
}

static inline unsigned __extent_ptr_durability(struct bch_dev *ca, struct extent_ptr_decoded *p)
{
	if (p->ptr.cached)
		return 0;

	return p->has_ec
		? p->ec.redundancy + 1
		: ca->mi.durability;
}

unsigned bch2_extent_ptr_desired_durability(struct bch_fs *c, struct extent_ptr_decoded *p)
{
	struct bch_dev *ca = bch2_dev_rcu(c, p->ptr.dev);

	return ca ? __extent_ptr_durability(ca, p) : 0;
}

unsigned bch2_extent_ptr_durability(struct bch_fs *c, struct extent_ptr_decoded *p)
{
	struct bch_dev *ca = bch2_dev_rcu(c, p->ptr.dev);

	if (!ca || ca->mi.state == BCH_MEMBER_STATE_failed)
		return 0;

	return __extent_ptr_durability(ca, p);
}

unsigned bch2_bkey_durability(struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned durability = 0;

	rcu_read_lock();
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
		durability += bch2_extent_ptr_durability(c, &p);
	rcu_read_unlock();

	return durability;
}

static unsigned bch2_bkey_durability_safe(struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned durability = 0;

	rcu_read_lock();
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
		if (p.ptr.dev < c->sb.nr_devices && c->devs[p.ptr.dev])
			durability += bch2_extent_ptr_durability(c, &p);
	rcu_read_unlock();

	return durability;
}

void bch2_bkey_extent_entry_drop(struct bkey_i *k, union bch_extent_entry *entry)
{
	union bch_extent_entry *end = bkey_val_end(bkey_i_to_s(k));
	union bch_extent_entry *next = extent_entry_next(entry);

	memmove_u64s(entry, next, (u64 *) end - (u64 *) next);
	k->k.u64s -= extent_entry_u64s(entry);
}

void bch2_extent_ptr_decoded_append(struct bkey_i *k,
				    struct extent_ptr_decoded *p)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(bkey_i_to_s(k));
	struct bch_extent_crc_unpacked crc =
		bch2_extent_crc_unpack(&k->k, NULL);
	union bch_extent_entry *pos;

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

	if (p->has_ec) {
		p->ec.type = 1 << BCH_EXTENT_ENTRY_stripe_ptr;
		__extent_entry_insert(k, pos, to_entry(&p->ec));
	}
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

/*
 * Returns pointer to the next entry after the one being dropped:
 */
union bch_extent_entry *bch2_bkey_drop_ptr_noerror(struct bkey_s k,
						   struct bch_extent_ptr *ptr)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(k);
	union bch_extent_entry *entry = to_entry(ptr), *next;
	union bch_extent_entry *ret = entry;
	bool drop_crc = true;

	EBUG_ON(ptr < &ptrs.start->ptr ||
		ptr >= &ptrs.end->ptr);
	EBUG_ON(ptr->type != 1 << BCH_EXTENT_ENTRY_ptr);

	for (next = extent_entry_next(entry);
	     next != ptrs.end;
	     next = extent_entry_next(next)) {
		if (extent_entry_is_crc(next)) {
			break;
		} else if (extent_entry_is_ptr(next)) {
			drop_crc = false;
			break;
		}
	}

	extent_entry_drop(k, entry);

	while ((entry = extent_entry_prev(ptrs, entry))) {
		if (extent_entry_is_ptr(entry))
			break;

		if ((extent_entry_is_crc(entry) && drop_crc) ||
		    extent_entry_is_stripe_ptr(entry)) {
			ret = (void *) ret - extent_entry_bytes(entry);
			extent_entry_drop(k, entry);
		}
	}

	return ret;
}

union bch_extent_entry *bch2_bkey_drop_ptr(struct bkey_s k,
					   struct bch_extent_ptr *ptr)
{
	bool have_dirty = bch2_bkey_dirty_devs(k.s_c).nr;
	union bch_extent_entry *ret =
		bch2_bkey_drop_ptr_noerror(k, ptr);

	/*
	 * If we deleted all the dirty pointers and there's still cached
	 * pointers, we could set the cached pointers to dirty if they're not
	 * stale - but to do that correctly we'd need to grab an open_bucket
	 * reference so that we don't race with bucket reuse:
	 */
	if (have_dirty &&
	    !bch2_bkey_dirty_devs(k.s_c).nr) {
		k.k->type = KEY_TYPE_error;
		set_bkey_val_u64s(k.k, 0);
		ret = NULL;
	} else if (!bch2_bkey_nr_ptrs(k.s_c)) {
		k.k->type = KEY_TYPE_deleted;
		set_bkey_val_u64s(k.k, 0);
		ret = NULL;
	}

	return ret;
}

void bch2_bkey_drop_device(struct bkey_s k, unsigned dev)
{
	bch2_bkey_drop_ptrs(k, ptr, ptr->dev == dev);
}

void bch2_bkey_drop_device_noerror(struct bkey_s k, unsigned dev)
{
	struct bch_extent_ptr *ptr = bch2_bkey_has_device(k, dev);

	if (ptr)
		bch2_bkey_drop_ptr_noerror(k, ptr);
}

const struct bch_extent_ptr *bch2_bkey_has_device_c(struct bkey_s_c k, unsigned dev)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);

	bkey_for_each_ptr(ptrs, ptr)
		if (ptr->dev == dev)
			return ptr;

	return NULL;
}

bool bch2_bkey_has_target(struct bch_fs *c, struct bkey_s_c k, unsigned target)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	struct bch_dev *ca;
	bool ret = false;

	rcu_read_lock();
	bkey_for_each_ptr(ptrs, ptr)
		if (bch2_dev_in_target(c, ptr->dev, target) &&
		    (ca = bch2_dev_rcu(c, ptr->dev)) &&
		    (!ptr->cached ||
		     !dev_ptr_stale_rcu(ca, ptr))) {
			ret = true;
			break;
		}
	rcu_read_unlock();

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

/*
 * Returns true if two extents refer to the same data:
 */
bool bch2_extents_match(struct bkey_s_c k1, struct bkey_s_c k2)
{
	if (k1.k->type != k2.k->type)
		return false;

	if (bkey_extent_is_direct_data(k1.k)) {
		struct bkey_ptrs_c ptrs1 = bch2_bkey_ptrs_c(k1);
		struct bkey_ptrs_c ptrs2 = bch2_bkey_ptrs_c(k2);
		const union bch_extent_entry *entry1, *entry2;
		struct extent_ptr_decoded p1, p2;

		if (bkey_extent_is_unwritten(k1) != bkey_extent_is_unwritten(k2))
			return false;

		bkey_for_each_ptr_decode(k1.k, ptrs1, p1, entry1)
			bkey_for_each_ptr_decode(k2.k, ptrs2, p2, entry2)
				if (p1.ptr.dev		== p2.ptr.dev &&
				    p1.ptr.gen		== p2.ptr.gen &&
				    (s64) p1.ptr.offset + p1.crc.offset - bkey_start_offset(k1.k) ==
				    (s64) p2.ptr.offset + p2.crc.offset - bkey_start_offset(k2.k))
					return true;

		return false;
	} else {
		/* KEY_TYPE_deleted, etc. */
		return true;
	}
}

struct bch_extent_ptr *
bch2_extent_has_ptr(struct bkey_s_c k1, struct extent_ptr_decoded p1, struct bkey_s k2)
{
	struct bkey_ptrs ptrs2 = bch2_bkey_ptrs(k2);
	union bch_extent_entry *entry2;
	struct extent_ptr_decoded p2;

	bkey_for_each_ptr_decode(k2.k, ptrs2, p2, entry2)
		if (p1.ptr.dev		== p2.ptr.dev &&
		    p1.ptr.gen		== p2.ptr.gen &&
		    (s64) p1.ptr.offset + p1.crc.offset - bkey_start_offset(k1.k) ==
		    (s64) p2.ptr.offset + p2.crc.offset - bkey_start_offset(k2.k))
			return &entry2->ptr;

	return NULL;
}

void bch2_extent_ptr_set_cached(struct bkey_s k, struct bch_extent_ptr *ptr)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(k);
	union bch_extent_entry *entry;
	union bch_extent_entry *ec = NULL;

	bkey_extent_entry_for_each(ptrs, entry) {
		if (&entry->ptr == ptr) {
			ptr->cached = true;
			if (ec)
				extent_entry_drop(k, ec);
			return;
		}

		if (extent_entry_is_stripe_ptr(entry))
			ec = entry;
		else if (extent_entry_is_ptr(entry))
			ec = NULL;
	}

	BUG();
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
	struct bch_dev *ca;

	rcu_read_lock();
	bch2_bkey_drop_ptrs(k, ptr,
		ptr->cached &&
		(ca = bch2_dev_rcu(c, ptr->dev)) &&
		dev_ptr_stale_rcu(ca, ptr));
	rcu_read_unlock();

	return bkey_deleted(k.k);
}

void bch2_extent_ptr_to_text(struct printbuf *out, struct bch_fs *c, const struct bch_extent_ptr *ptr)
{
	out->atomic++;
	rcu_read_lock();
	struct bch_dev *ca = bch2_dev_rcu(c, ptr->dev);
	if (!ca) {
		prt_printf(out, "ptr: %u:%llu gen %u%s", ptr->dev,
			   (u64) ptr->offset, ptr->gen,
			   ptr->cached ? " cached" : "");
	} else {
		u32 offset;
		u64 b = sector_to_bucket_and_offset(ca, ptr->offset, &offset);

		prt_printf(out, "ptr: %u:%llu:%u gen %u",
			   ptr->dev, b, offset, ptr->gen);
		if (ptr->cached)
			prt_str(out, " cached");
		if (ptr->unwritten)
			prt_str(out, " unwritten");
		if (bucket_valid(ca, b) && dev_ptr_stale_rcu(ca, ptr))
			prt_printf(out, " stale");
	}
	rcu_read_unlock();
	--out->atomic;
}

void bch2_bkey_ptrs_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	bool first = true;

	if (c)
		prt_printf(out, "durability: %u ", bch2_bkey_durability_safe(c, k));

	bkey_extent_entry_for_each(ptrs, entry) {
		if (!first)
			prt_printf(out, " ");

		switch (__extent_entry_type(entry)) {
		case BCH_EXTENT_ENTRY_ptr:
			bch2_extent_ptr_to_text(out, c, entry_to_ptr(entry));
			break;

		case BCH_EXTENT_ENTRY_crc32:
		case BCH_EXTENT_ENTRY_crc64:
		case BCH_EXTENT_ENTRY_crc128: {
			struct bch_extent_crc_unpacked crc =
				bch2_extent_crc_unpack(k.k, entry_to_crc(entry));

			prt_printf(out, "crc: c_size %u size %u offset %u nonce %u csum ",
			       crc.compressed_size,
			       crc.uncompressed_size,
			       crc.offset, crc.nonce);
			bch2_prt_csum_type(out, crc.csum_type);
			prt_str(out, " compress ");
			bch2_prt_compression_type(out, crc.compression_type);
			break;
		}
		case BCH_EXTENT_ENTRY_stripe_ptr: {
			const struct bch_extent_stripe_ptr *ec = &entry->stripe_ptr;

			prt_printf(out, "ec: idx %llu block %u",
			       (u64) ec->idx, ec->block);
			break;
		}
		case BCH_EXTENT_ENTRY_rebalance: {
			const struct bch_extent_rebalance *r = &entry->rebalance;

			prt_str(out, "rebalance: target ");
			if (c)
				bch2_target_to_text(out, c, r->target);
			else
				prt_printf(out, "%u", r->target);
			prt_str(out, " compression ");
			bch2_compression_opt_to_text(out, r->compression);
			break;
		}
		default:
			prt_printf(out, "(invalid extent entry %.16llx)", *((u64 *) entry));
			return;
		}

		first = false;
	}
}

static int extent_ptr_invalid(struct bch_fs *c,
			      struct bkey_s_c k,
			      enum bch_validate_flags flags,
			      const struct bch_extent_ptr *ptr,
			      unsigned size_ondisk,
			      bool metadata,
			      struct printbuf *err)
{
	int ret = 0;

	rcu_read_lock();
	struct bch_dev *ca = bch2_dev_rcu(c, ptr->dev);
	if (!ca) {
		rcu_read_unlock();
		return 0;
	}
	u32 bucket_offset;
	u64 bucket = sector_to_bucket_and_offset(ca, ptr->offset, &bucket_offset);
	unsigned first_bucket	= ca->mi.first_bucket;
	u64 nbuckets		= ca->mi.nbuckets;
	unsigned bucket_size	= ca->mi.bucket_size;
	rcu_read_unlock();

	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	bkey_for_each_ptr(ptrs, ptr2)
		bkey_fsck_err_on(ptr != ptr2 && ptr->dev == ptr2->dev, c, err,
				 ptr_to_duplicate_device,
				 "multiple pointers to same device (%u)", ptr->dev);


	bkey_fsck_err_on(bucket >= nbuckets, c, err,
			 ptr_after_last_bucket,
			 "pointer past last bucket (%llu > %llu)", bucket, nbuckets);
	bkey_fsck_err_on(bucket < first_bucket, c, err,
			 ptr_before_first_bucket,
			 "pointer before first bucket (%llu < %u)", bucket, first_bucket);
	bkey_fsck_err_on(bucket_offset + size_ondisk > bucket_size, c, err,
			 ptr_spans_multiple_buckets,
			 "pointer spans multiple buckets (%u + %u > %u)",
		       bucket_offset, size_ondisk, bucket_size);
fsck_err:
	return ret;
}

int bch2_bkey_ptrs_invalid(struct bch_fs *c, struct bkey_s_c k,
			   enum bch_validate_flags flags,
			   struct printbuf *err)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct bch_extent_crc_unpacked crc;
	unsigned size_ondisk = k.k->size;
	unsigned nonce = UINT_MAX;
	unsigned nr_ptrs = 0;
	bool have_written = false, have_unwritten = false, have_ec = false, crc_since_last_ptr = false;
	int ret = 0;

	if (bkey_is_btree_ptr(k.k))
		size_ondisk = btree_sectors(c);

	bkey_extent_entry_for_each(ptrs, entry) {
		bkey_fsck_err_on(__extent_entry_type(entry) >= BCH_EXTENT_ENTRY_MAX, c, err,
			extent_ptrs_invalid_entry,
			"invalid extent entry type (got %u, max %u)",
			__extent_entry_type(entry), BCH_EXTENT_ENTRY_MAX);

		bkey_fsck_err_on(bkey_is_btree_ptr(k.k) &&
				 !extent_entry_is_ptr(entry), c, err,
				 btree_ptr_has_non_ptr,
				 "has non ptr field");

		switch (extent_entry_type(entry)) {
		case BCH_EXTENT_ENTRY_ptr:
			ret = extent_ptr_invalid(c, k, flags, &entry->ptr,
						 size_ondisk, false, err);
			if (ret)
				return ret;

			bkey_fsck_err_on(entry->ptr.cached && have_ec, c, err,
					 ptr_cached_and_erasure_coded,
					 "cached, erasure coded ptr");

			if (!entry->ptr.unwritten)
				have_written = true;
			else
				have_unwritten = true;

			have_ec = false;
			crc_since_last_ptr = false;
			nr_ptrs++;
			break;
		case BCH_EXTENT_ENTRY_crc32:
		case BCH_EXTENT_ENTRY_crc64:
		case BCH_EXTENT_ENTRY_crc128:
			crc = bch2_extent_crc_unpack(k.k, entry_to_crc(entry));

			bkey_fsck_err_on(crc.offset + crc.live_size > crc.uncompressed_size, c, err,
					 ptr_crc_uncompressed_size_too_small,
					 "checksum offset + key size > uncompressed size");
			bkey_fsck_err_on(!bch2_checksum_type_valid(c, crc.csum_type), c, err,
					 ptr_crc_csum_type_unknown,
					 "invalid checksum type");
			bkey_fsck_err_on(crc.compression_type >= BCH_COMPRESSION_TYPE_NR, c, err,
					 ptr_crc_compression_type_unknown,
					 "invalid compression type");

			if (bch2_csum_type_is_encryption(crc.csum_type)) {
				if (nonce == UINT_MAX)
					nonce = crc.offset + crc.nonce;
				else if (nonce != crc.offset + crc.nonce)
					bkey_fsck_err(c, err, ptr_crc_nonce_mismatch,
						      "incorrect nonce");
			}

			bkey_fsck_err_on(crc_since_last_ptr, c, err,
					 ptr_crc_redundant,
					 "redundant crc entry");
			crc_since_last_ptr = true;

			bkey_fsck_err_on(crc_is_encoded(crc) &&
					 (crc.uncompressed_size > c->opts.encoded_extent_max >> 9) &&
					 (flags & (BCH_VALIDATE_write|BCH_VALIDATE_commit)), c, err,
					 ptr_crc_uncompressed_size_too_big,
					 "too large encoded extent");

			size_ondisk = crc.compressed_size;
			break;
		case BCH_EXTENT_ENTRY_stripe_ptr:
			bkey_fsck_err_on(have_ec, c, err,
					 ptr_stripe_redundant,
					 "redundant stripe entry");
			have_ec = true;
			break;
		case BCH_EXTENT_ENTRY_rebalance: {
			const struct bch_extent_rebalance *r = &entry->rebalance;

			if (!bch2_compression_opt_valid(r->compression)) {
				struct bch_compression_opt opt = __bch2_compression_decode(r->compression);
				prt_printf(err, "invalid compression opt %u:%u",
					   opt.type, opt.level);
				return -BCH_ERR_invalid_bkey;
			}
			break;
		}
		}
	}

	bkey_fsck_err_on(!nr_ptrs, c, err,
			 extent_ptrs_no_ptrs,
			 "no ptrs");
	bkey_fsck_err_on(nr_ptrs > BCH_BKEY_PTRS_MAX, c, err,
			 extent_ptrs_too_many_ptrs,
			 "too many ptrs: %u > %u", nr_ptrs, BCH_BKEY_PTRS_MAX);
	bkey_fsck_err_on(have_written && have_unwritten, c, err,
			 extent_ptrs_written_and_unwritten,
			 "extent with unwritten and written ptrs");
	bkey_fsck_err_on(k.k->type != KEY_TYPE_extent && have_unwritten, c, err,
			 extent_ptrs_unwritten,
			 "has unwritten ptrs");
	bkey_fsck_err_on(crc_since_last_ptr, c, err,
			 extent_ptrs_redundant_crc,
			 "redundant crc entry");
	bkey_fsck_err_on(have_ec, c, err,
			 extent_ptrs_redundant_stripe,
			 "redundant stripe entry");
fsck_err:
	return ret;
}

void bch2_ptr_swab(struct bkey_s k)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(k);
	union bch_extent_entry *entry;
	u64 *d;

	for (d =  (u64 *) ptrs.start;
	     d != (u64 *) ptrs.end;
	     d++)
		*d = swab64(*d);

	for (entry = ptrs.start;
	     entry < ptrs.end;
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
		case BCH_EXTENT_ENTRY_rebalance:
			break;
		}
	}
}

const struct bch_extent_rebalance *bch2_bkey_rebalance_opts(struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;

	bkey_extent_entry_for_each(ptrs, entry)
		if (__extent_entry_type(entry) == BCH_EXTENT_ENTRY_rebalance)
			return &entry->rebalance;

	return NULL;
}

unsigned bch2_bkey_ptrs_need_rebalance(struct bch_fs *c, struct bkey_s_c k,
				       unsigned target, unsigned compression)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	unsigned rewrite_ptrs = 0;

	if (compression) {
		unsigned compression_type = bch2_compression_opt_to_type(compression);
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;
		unsigned i = 0;

		bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
			if (p.crc.compression_type == BCH_COMPRESSION_TYPE_incompressible ||
			    p.ptr.unwritten) {
				rewrite_ptrs = 0;
				goto incompressible;
			}

			if (!p.ptr.cached && p.crc.compression_type != compression_type)
				rewrite_ptrs |= 1U << i;
			i++;
		}
	}
incompressible:
	if (target && bch2_target_accepts_data(c, BCH_DATA_user, target)) {
		unsigned i = 0;

		bkey_for_each_ptr(ptrs, ptr) {
			if (!ptr->cached && !bch2_dev_in_target(c, ptr->dev, target))
				rewrite_ptrs |= 1U << i;
			i++;
		}
	}

	return rewrite_ptrs;
}

bool bch2_bkey_needs_rebalance(struct bch_fs *c, struct bkey_s_c k)
{
	const struct bch_extent_rebalance *r = bch2_bkey_rebalance_opts(k);

	/*
	 * If it's an indirect extent, we don't delete the rebalance entry when
	 * done so that we know what options were applied - check if it still
	 * needs work done:
	 */
	if (r &&
	    k.k->type == KEY_TYPE_reflink_v &&
	    !bch2_bkey_ptrs_need_rebalance(c, k, r->target, r->compression))
		r = NULL;

	return r != NULL;
}

int bch2_bkey_set_needs_rebalance(struct bch_fs *c, struct bkey_i *_k,
				  struct bch_io_opts *opts)
{
	struct bkey_s k = bkey_i_to_s(_k);
	struct bch_extent_rebalance *r;
	unsigned target = opts->background_target;
	unsigned compression = background_compression(*opts);
	bool needs_rebalance;

	if (!bkey_extent_is_direct_data(k.k))
		return 0;

	/* get existing rebalance entry: */
	r = (struct bch_extent_rebalance *) bch2_bkey_rebalance_opts(k.s_c);
	if (r) {
		if (k.k->type == KEY_TYPE_reflink_v) {
			/*
			 * indirect extents: existing options take precedence,
			 * so that we don't move extents back and forth if
			 * they're referenced by different inodes with different
			 * options:
			 */
			if (r->target)
				target = r->target;
			if (r->compression)
				compression = r->compression;
		}

		r->target	= target;
		r->compression	= compression;
	}

	needs_rebalance = bch2_bkey_ptrs_need_rebalance(c, k.s_c, target, compression);

	if (needs_rebalance && !r) {
		union bch_extent_entry *new = bkey_val_end(k);

		new->rebalance.type		= 1U << BCH_EXTENT_ENTRY_rebalance;
		new->rebalance.compression	= compression;
		new->rebalance.target		= target;
		new->rebalance.unused		= 0;
		k.k->u64s += extent_entry_u64s(new);
	} else if (!needs_rebalance && r && k.k->type != KEY_TYPE_reflink_v) {
		/*
		 * For indirect extents, don't delete the rebalance entry when
		 * we're finished so that we know we specifically moved it or
		 * compressed it to its current location/compression type
		 */
		extent_entry_drop(k, (union bch_extent_entry *) r);
	}

	return 0;
}

/* Generic extent code: */

int bch2_cut_front_s(struct bpos where, struct bkey_s k)
{
	unsigned new_val_u64s = bkey_val_u64s(k.k);
	int val_u64s_delta;
	u64 sub;

	if (bkey_le(where, bkey_start_pos(k.k)))
		return 0;

	EBUG_ON(bkey_gt(where, k.k->p));

	sub = where.offset - bkey_start_offset(k.k);

	k.k->size -= sub;

	if (!k.k->size) {
		k.k->type = KEY_TYPE_deleted;
		new_val_u64s = 0;
	}

	switch (k.k->type) {
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
			case BCH_EXTENT_ENTRY_rebalance:
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
	case KEY_TYPE_inline_data:
	case KEY_TYPE_indirect_inline_data: {
		void *p = bkey_inline_data_p(k);
		unsigned bytes = bkey_inline_data_bytes(k.k);

		sub = min_t(u64, sub << 9, bytes);

		memmove(p, p + sub, bytes - sub);

		new_val_u64s -= sub >> 3;
		break;
	}
	}

	val_u64s_delta = bkey_val_u64s(k.k) - new_val_u64s;
	BUG_ON(val_u64s_delta < 0);

	set_bkey_val_u64s(k.k, new_val_u64s);
	memset(bkey_val_end(k), 0, val_u64s_delta * sizeof(u64));
	return -val_u64s_delta;
}

int bch2_cut_back_s(struct bpos where, struct bkey_s k)
{
	unsigned new_val_u64s = bkey_val_u64s(k.k);
	int val_u64s_delta;
	u64 len = 0;

	if (bkey_ge(where, k.k->p))
		return 0;

	EBUG_ON(bkey_lt(where, bkey_start_pos(k.k)));

	len = where.offset - bkey_start_offset(k.k);

	k.k->p.offset = where.offset;
	k.k->size = len;

	if (!len) {
		k.k->type = KEY_TYPE_deleted;
		new_val_u64s = 0;
	}

	switch (k.k->type) {
	case KEY_TYPE_inline_data:
	case KEY_TYPE_indirect_inline_data:
		new_val_u64s = (bkey_inline_data_offset(k.k) +
				min(bkey_inline_data_bytes(k.k), k.k->size << 9)) >> 3;
		break;
	}

	val_u64s_delta = bkey_val_u64s(k.k) - new_val_u64s;
	BUG_ON(val_u64s_delta < 0);

	set_bkey_val_u64s(k.k, new_val_u64s);
	memset(bkey_val_end(k), 0, val_u64s_delta * sizeof(u64));
	return -val_u64s_delta;
}
