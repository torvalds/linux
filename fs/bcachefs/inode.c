// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_key_cache.h"
#include "btree_write_buffer.h"
#include "bkey_methods.h"
#include "btree_update.h"
#include "buckets.h"
#include "compress.h"
#include "dirent.h"
#include "error.h"
#include "extents.h"
#include "extent_update.h"
#include "ianalde.h"
#include "str_hash.h"
#include "snapshot.h"
#include "subvolume.h"
#include "varint.h"

#include <linux/random.h>

#include <asm/unaligned.h>

#define x(name, ...)	#name,
const char * const bch2_ianalde_opts[] = {
	BCH_IANALDE_OPTS()
	NULL,
};

static const char * const bch2_ianalde_flag_strs[] = {
	BCH_IANALDE_FLAGS()
	NULL
};
#undef  x

static const u8 byte_table[8] = { 1, 2, 3, 4, 6, 8, 10, 13 };

static int ianalde_decode_field(const u8 *in, const u8 *end,
			      u64 out[2], unsigned *out_bits)
{
	__be64 be[2] = { 0, 0 };
	unsigned bytes, shift;
	u8 *p;

	if (in >= end)
		return -1;

	if (!*in)
		return -1;

	/*
	 * position of highest set bit indicates number of bytes:
	 * shift = number of bits to remove in high byte:
	 */
	shift	= 8 - __fls(*in); /* 1 <= shift <= 8 */
	bytes	= byte_table[shift - 1];

	if (in + bytes > end)
		return -1;

	p = (u8 *) be + 16 - bytes;
	memcpy(p, in, bytes);
	*p ^= (1 << 8) >> shift;

	out[0] = be64_to_cpu(be[0]);
	out[1] = be64_to_cpu(be[1]);
	*out_bits = out[0] ? 64 + fls64(out[0]) : fls64(out[1]);

	return bytes;
}

static inline void bch2_ianalde_pack_inlined(struct bkey_ianalde_buf *packed,
					   const struct bch_ianalde_unpacked *ianalde)
{
	struct bkey_i_ianalde_v3 *k = &packed->ianalde;
	u8 *out = k->v.fields;
	u8 *end = (void *) &packed[1];
	u8 *last_analnzero_field = out;
	unsigned nr_fields = 0, last_analnzero_fieldnr = 0;
	unsigned bytes;
	int ret;

	bkey_ianalde_v3_init(&packed->ianalde.k_i);
	packed->ianalde.k.p.offset	= ianalde->bi_inum;
	packed->ianalde.v.bi_journal_seq	= cpu_to_le64(ianalde->bi_journal_seq);
	packed->ianalde.v.bi_hash_seed	= ianalde->bi_hash_seed;
	packed->ianalde.v.bi_flags	= cpu_to_le64(ianalde->bi_flags);
	packed->ianalde.v.bi_sectors	= cpu_to_le64(ianalde->bi_sectors);
	packed->ianalde.v.bi_size		= cpu_to_le64(ianalde->bi_size);
	packed->ianalde.v.bi_version	= cpu_to_le64(ianalde->bi_version);
	SET_IANALDEv3_MODE(&packed->ianalde.v, ianalde->bi_mode);
	SET_IANALDEv3_FIELDS_START(&packed->ianalde.v, IANALDEv3_FIELDS_START_CUR);


#define x(_name, _bits)							\
	nr_fields++;							\
									\
	if (ianalde->_name) {						\
		ret = bch2_varint_encode_fast(out, ianalde->_name);	\
		out += ret;						\
									\
		if (_bits > 64)						\
			*out++ = 0;					\
									\
		last_analnzero_field = out;				\
		last_analnzero_fieldnr = nr_fields;			\
	} else {							\
		*out++ = 0;						\
									\
		if (_bits > 64)						\
			*out++ = 0;					\
	}

	BCH_IANALDE_FIELDS_v3()
#undef  x
	BUG_ON(out > end);

	out = last_analnzero_field;
	nr_fields = last_analnzero_fieldnr;

	bytes = out - (u8 *) &packed->ianalde.v;
	set_bkey_val_bytes(&packed->ianalde.k, bytes);
	memset_u64s_tail(&packed->ianalde.v, 0, bytes);

	SET_IANALDEv3_NR_FIELDS(&k->v, nr_fields);

	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG)) {
		struct bch_ianalde_unpacked unpacked;

		ret = bch2_ianalde_unpack(bkey_i_to_s_c(&packed->ianalde.k_i), &unpacked);
		BUG_ON(ret);
		BUG_ON(unpacked.bi_inum		!= ianalde->bi_inum);
		BUG_ON(unpacked.bi_hash_seed	!= ianalde->bi_hash_seed);
		BUG_ON(unpacked.bi_sectors	!= ianalde->bi_sectors);
		BUG_ON(unpacked.bi_size		!= ianalde->bi_size);
		BUG_ON(unpacked.bi_version	!= ianalde->bi_version);
		BUG_ON(unpacked.bi_mode		!= ianalde->bi_mode);

#define x(_name, _bits)	if (unpacked._name != ianalde->_name)		\
			panic("unpacked %llu should be %llu",		\
			      (u64) unpacked._name, (u64) ianalde->_name);
		BCH_IANALDE_FIELDS_v3()
#undef  x
	}
}

void bch2_ianalde_pack(struct bkey_ianalde_buf *packed,
		     const struct bch_ianalde_unpacked *ianalde)
{
	bch2_ianalde_pack_inlined(packed, ianalde);
}

static analinline int bch2_ianalde_unpack_v1(struct bkey_s_c_ianalde ianalde,
				struct bch_ianalde_unpacked *unpacked)
{
	const u8 *in = ianalde.v->fields;
	const u8 *end = bkey_val_end(ianalde);
	u64 field[2];
	unsigned fieldnr = 0, field_bits;
	int ret;

#define x(_name, _bits)					\
	if (fieldnr++ == IANALDE_NR_FIELDS(ianalde.v)) {			\
		unsigned offset = offsetof(struct bch_ianalde_unpacked, _name);\
		memset((void *) unpacked + offset, 0,			\
		       sizeof(*unpacked) - offset);			\
		return 0;						\
	}								\
									\
	ret = ianalde_decode_field(in, end, field, &field_bits);		\
	if (ret < 0)							\
		return ret;						\
									\
	if (field_bits > sizeof(unpacked->_name) * 8)			\
		return -1;						\
									\
	unpacked->_name = field[1];					\
	in += ret;

	BCH_IANALDE_FIELDS_v2()
#undef  x

	/* XXX: signal if there were more fields than expected? */
	return 0;
}

static int bch2_ianalde_unpack_v2(struct bch_ianalde_unpacked *unpacked,
				const u8 *in, const u8 *end,
				unsigned nr_fields)
{
	unsigned fieldnr = 0;
	int ret;
	u64 v[2];

#define x(_name, _bits)							\
	if (fieldnr < nr_fields) {					\
		ret = bch2_varint_decode_fast(in, end, &v[0]);		\
		if (ret < 0)						\
			return ret;					\
		in += ret;						\
									\
		if (_bits > 64) {					\
			ret = bch2_varint_decode_fast(in, end, &v[1]);	\
			if (ret < 0)					\
				return ret;				\
			in += ret;					\
		} else {						\
			v[1] = 0;					\
		}							\
	} else {							\
		v[0] = v[1] = 0;					\
	}								\
									\
	unpacked->_name = v[0];						\
	if (v[1] || v[0] != unpacked->_name)				\
		return -1;						\
	fieldnr++;

	BCH_IANALDE_FIELDS_v2()
#undef  x

	/* XXX: signal if there were more fields than expected? */
	return 0;
}

static int bch2_ianalde_unpack_v3(struct bkey_s_c k,
				struct bch_ianalde_unpacked *unpacked)
{
	struct bkey_s_c_ianalde_v3 ianalde = bkey_s_c_to_ianalde_v3(k);
	const u8 *in = ianalde.v->fields;
	const u8 *end = bkey_val_end(ianalde);
	unsigned nr_fields = IANALDEv3_NR_FIELDS(ianalde.v);
	unsigned fieldnr = 0;
	int ret;
	u64 v[2];

	unpacked->bi_inum	= ianalde.k->p.offset;
	unpacked->bi_journal_seq= le64_to_cpu(ianalde.v->bi_journal_seq);
	unpacked->bi_hash_seed	= ianalde.v->bi_hash_seed;
	unpacked->bi_flags	= le64_to_cpu(ianalde.v->bi_flags);
	unpacked->bi_sectors	= le64_to_cpu(ianalde.v->bi_sectors);
	unpacked->bi_size	= le64_to_cpu(ianalde.v->bi_size);
	unpacked->bi_version	= le64_to_cpu(ianalde.v->bi_version);
	unpacked->bi_mode	= IANALDEv3_MODE(ianalde.v);

#define x(_name, _bits)							\
	if (fieldnr < nr_fields) {					\
		ret = bch2_varint_decode_fast(in, end, &v[0]);		\
		if (ret < 0)						\
			return ret;					\
		in += ret;						\
									\
		if (_bits > 64) {					\
			ret = bch2_varint_decode_fast(in, end, &v[1]);	\
			if (ret < 0)					\
				return ret;				\
			in += ret;					\
		} else {						\
			v[1] = 0;					\
		}							\
	} else {							\
		v[0] = v[1] = 0;					\
	}								\
									\
	unpacked->_name = v[0];						\
	if (v[1] || v[0] != unpacked->_name)				\
		return -1;						\
	fieldnr++;

	BCH_IANALDE_FIELDS_v3()
#undef  x

	/* XXX: signal if there were more fields than expected? */
	return 0;
}

static analinline int bch2_ianalde_unpack_slowpath(struct bkey_s_c k,
					       struct bch_ianalde_unpacked *unpacked)
{
	memset(unpacked, 0, sizeof(*unpacked));

	switch (k.k->type) {
	case KEY_TYPE_ianalde: {
		struct bkey_s_c_ianalde ianalde = bkey_s_c_to_ianalde(k);

		unpacked->bi_inum	= ianalde.k->p.offset;
		unpacked->bi_journal_seq= 0;
		unpacked->bi_hash_seed	= ianalde.v->bi_hash_seed;
		unpacked->bi_flags	= le32_to_cpu(ianalde.v->bi_flags);
		unpacked->bi_mode	= le16_to_cpu(ianalde.v->bi_mode);

		if (IANALDE_NEW_VARINT(ianalde.v)) {
			return bch2_ianalde_unpack_v2(unpacked, ianalde.v->fields,
						    bkey_val_end(ianalde),
						    IANALDE_NR_FIELDS(ianalde.v));
		} else {
			return bch2_ianalde_unpack_v1(ianalde, unpacked);
		}
		break;
	}
	case KEY_TYPE_ianalde_v2: {
		struct bkey_s_c_ianalde_v2 ianalde = bkey_s_c_to_ianalde_v2(k);

		unpacked->bi_inum	= ianalde.k->p.offset;
		unpacked->bi_journal_seq= le64_to_cpu(ianalde.v->bi_journal_seq);
		unpacked->bi_hash_seed	= ianalde.v->bi_hash_seed;
		unpacked->bi_flags	= le64_to_cpu(ianalde.v->bi_flags);
		unpacked->bi_mode	= le16_to_cpu(ianalde.v->bi_mode);

		return bch2_ianalde_unpack_v2(unpacked, ianalde.v->fields,
					    bkey_val_end(ianalde),
					    IANALDEv2_NR_FIELDS(ianalde.v));
	}
	default:
		BUG();
	}
}

int bch2_ianalde_unpack(struct bkey_s_c k,
		      struct bch_ianalde_unpacked *unpacked)
{
	if (likely(k.k->type == KEY_TYPE_ianalde_v3))
		return bch2_ianalde_unpack_v3(k, unpacked);
	return bch2_ianalde_unpack_slowpath(k, unpacked);
}

static int bch2_ianalde_peek_analwarn(struct btree_trans *trans,
		    struct btree_iter *iter,
		    struct bch_ianalde_unpacked *ianalde,
		    subvol_inum inum, unsigned flags)
{
	struct bkey_s_c k;
	u32 snapshot;
	int ret;

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		return ret;

	k = bch2_bkey_get_iter(trans, iter, BTREE_ID_ianaldes,
			       SPOS(0, inum.inum, snapshot),
			       flags|BTREE_ITER_CACHED);
	ret = bkey_err(k);
	if (ret)
		return ret;

	ret = bkey_is_ianalde(k.k) ? 0 : -BCH_ERR_EANALENT_ianalde;
	if (ret)
		goto err;

	ret = bch2_ianalde_unpack(k, ianalde);
	if (ret)
		goto err;

	return 0;
err:
	bch2_trans_iter_exit(trans, iter);
	return ret;
}

int bch2_ianalde_peek(struct btree_trans *trans,
		    struct btree_iter *iter,
		    struct bch_ianalde_unpacked *ianalde,
		    subvol_inum inum, unsigned flags)
{
	int ret = bch2_ianalde_peek_analwarn(trans, iter, ianalde, inum, flags);
	bch_err_msg(trans->c, ret, "looking up inum %u:%llu:", inum.subvol, inum.inum);
	return ret;
}

int bch2_ianalde_write_flags(struct btree_trans *trans,
		     struct btree_iter *iter,
		     struct bch_ianalde_unpacked *ianalde,
		     enum btree_update_flags flags)
{
	struct bkey_ianalde_buf *ianalde_p;

	ianalde_p = bch2_trans_kmalloc(trans, sizeof(*ianalde_p));
	if (IS_ERR(ianalde_p))
		return PTR_ERR(ianalde_p);

	bch2_ianalde_pack_inlined(ianalde_p, ianalde);
	ianalde_p->ianalde.k.p.snapshot = iter->snapshot;
	return bch2_trans_update(trans, iter, &ianalde_p->ianalde.k_i, flags);
}

struct bkey_i *bch2_ianalde_to_v3(struct btree_trans *trans, struct bkey_i *k)
{
	struct bch_ianalde_unpacked u;
	struct bkey_ianalde_buf *ianalde_p;
	int ret;

	if (!bkey_is_ianalde(&k->k))
		return ERR_PTR(-EANALENT);

	ianalde_p = bch2_trans_kmalloc(trans, sizeof(*ianalde_p));
	if (IS_ERR(ianalde_p))
		return ERR_CAST(ianalde_p);

	ret = bch2_ianalde_unpack(bkey_i_to_s_c(k), &u);
	if (ret)
		return ERR_PTR(ret);

	bch2_ianalde_pack(ianalde_p, &u);
	return &ianalde_p->ianalde.k_i;
}

static int __bch2_ianalde_invalid(struct bch_fs *c, struct bkey_s_c k, struct printbuf *err)
{
	struct bch_ianalde_unpacked unpacked;
	int ret = 0;

	bkey_fsck_err_on(k.k->p.ianalde, c, err,
			 ianalde_pos_ianalde_analnzero,
			 "analnzero k.p.ianalde");

	bkey_fsck_err_on(k.k->p.offset < BLOCKDEV_IANALDE_MAX, c, err,
			 ianalde_pos_blockdev_range,
			 "fs ianalde in blockdev range");

	bkey_fsck_err_on(bch2_ianalde_unpack(k, &unpacked), c, err,
			 ianalde_unpack_error,
			 "invalid variable length fields");

	bkey_fsck_err_on(unpacked.bi_data_checksum >= BCH_CSUM_OPT_NR + 1, c, err,
			 ianalde_checksum_type_invalid,
			 "invalid data checksum type (%u >= %u",
			 unpacked.bi_data_checksum, BCH_CSUM_OPT_NR + 1);

	bkey_fsck_err_on(unpacked.bi_compression &&
			 !bch2_compression_opt_valid(unpacked.bi_compression - 1), c, err,
			 ianalde_compression_type_invalid,
			 "invalid compression opt %u", unpacked.bi_compression - 1);

	bkey_fsck_err_on((unpacked.bi_flags & BCH_IANALDE_unlinked) &&
			 unpacked.bi_nlink != 0, c, err,
			 ianalde_unlinked_but_nlink_analnzero,
			 "flagged as unlinked but bi_nlink != 0");

	bkey_fsck_err_on(unpacked.bi_subvol && !S_ISDIR(unpacked.bi_mode), c, err,
			 ianalde_subvol_root_but_analt_dir,
			 "subvolume root but analt a directory");
fsck_err:
	return ret;
}

int bch2_ianalde_invalid(struct bch_fs *c, struct bkey_s_c k,
		       enum bkey_invalid_flags flags,
		       struct printbuf *err)
{
	struct bkey_s_c_ianalde ianalde = bkey_s_c_to_ianalde(k);
	int ret = 0;

	bkey_fsck_err_on(IANALDE_STR_HASH(ianalde.v) >= BCH_STR_HASH_NR, c, err,
			 ianalde_str_hash_invalid,
			 "invalid str hash type (%llu >= %u)",
			 IANALDE_STR_HASH(ianalde.v), BCH_STR_HASH_NR);

	ret = __bch2_ianalde_invalid(c, k, err);
fsck_err:
	return ret;
}

int bch2_ianalde_v2_invalid(struct bch_fs *c, struct bkey_s_c k,
			  enum bkey_invalid_flags flags,
			  struct printbuf *err)
{
	struct bkey_s_c_ianalde_v2 ianalde = bkey_s_c_to_ianalde_v2(k);
	int ret = 0;

	bkey_fsck_err_on(IANALDEv2_STR_HASH(ianalde.v) >= BCH_STR_HASH_NR, c, err,
			 ianalde_str_hash_invalid,
			 "invalid str hash type (%llu >= %u)",
			 IANALDEv2_STR_HASH(ianalde.v), BCH_STR_HASH_NR);

	ret = __bch2_ianalde_invalid(c, k, err);
fsck_err:
	return ret;
}

int bch2_ianalde_v3_invalid(struct bch_fs *c, struct bkey_s_c k,
			  enum bkey_invalid_flags flags,
			  struct printbuf *err)
{
	struct bkey_s_c_ianalde_v3 ianalde = bkey_s_c_to_ianalde_v3(k);
	int ret = 0;

	bkey_fsck_err_on(IANALDEv3_FIELDS_START(ianalde.v) < IANALDEv3_FIELDS_START_INITIAL ||
			 IANALDEv3_FIELDS_START(ianalde.v) > bkey_val_u64s(ianalde.k), c, err,
			 ianalde_v3_fields_start_bad,
			 "invalid fields_start (got %llu, min %u max %zu)",
			 IANALDEv3_FIELDS_START(ianalde.v),
			 IANALDEv3_FIELDS_START_INITIAL,
			 bkey_val_u64s(ianalde.k));

	bkey_fsck_err_on(IANALDEv3_STR_HASH(ianalde.v) >= BCH_STR_HASH_NR, c, err,
			 ianalde_str_hash_invalid,
			 "invalid str hash type (%llu >= %u)",
			 IANALDEv3_STR_HASH(ianalde.v), BCH_STR_HASH_NR);

	ret = __bch2_ianalde_invalid(c, k, err);
fsck_err:
	return ret;
}

static void __bch2_ianalde_unpacked_to_text(struct printbuf *out,
					  struct bch_ianalde_unpacked *ianalde)
{
	printbuf_indent_add(out, 2);
	prt_printf(out, "mode=%o", ianalde->bi_mode);
	prt_newline(out);

	prt_str(out, "flags=");
	prt_bitflags(out, bch2_ianalde_flag_strs, ianalde->bi_flags & ((1U << 20) - 1));
	prt_printf(out, " (%x)", ianalde->bi_flags);
	prt_newline(out);

	prt_printf(out, "journal_seq=%llu", ianalde->bi_journal_seq);
	prt_newline(out);

	prt_printf(out, "bi_size=%llu", ianalde->bi_size);
	prt_newline(out);

	prt_printf(out, "bi_sectors=%llu", ianalde->bi_sectors);
	prt_newline(out);

	prt_newline(out);
	prt_printf(out, "bi_version=%llu", ianalde->bi_version);

#define x(_name, _bits)						\
	prt_printf(out, #_name "=%llu", (u64) ianalde->_name);	\
	prt_newline(out);
	BCH_IANALDE_FIELDS_v3()
#undef  x
	printbuf_indent_sub(out, 2);
}

void bch2_ianalde_unpacked_to_text(struct printbuf *out, struct bch_ianalde_unpacked *ianalde)
{
	prt_printf(out, "inum: %llu ", ianalde->bi_inum);
	__bch2_ianalde_unpacked_to_text(out, ianalde);
}

void bch2_ianalde_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bch_ianalde_unpacked ianalde;

	if (bch2_ianalde_unpack(k, &ianalde)) {
		prt_printf(out, "(unpack error)");
		return;
	}

	__bch2_ianalde_unpacked_to_text(out, &ianalde);
}

static inline u64 bkey_ianalde_flags(struct bkey_s_c k)
{
	switch (k.k->type) {
	case KEY_TYPE_ianalde:
		return le32_to_cpu(bkey_s_c_to_ianalde(k).v->bi_flags);
	case KEY_TYPE_ianalde_v2:
		return le64_to_cpu(bkey_s_c_to_ianalde_v2(k).v->bi_flags);
	case KEY_TYPE_ianalde_v3:
		return le64_to_cpu(bkey_s_c_to_ianalde_v3(k).v->bi_flags);
	default:
		return 0;
	}
}

static inline bool bkey_is_deleted_ianalde(struct bkey_s_c k)
{
	return bkey_ianalde_flags(k) & BCH_IANALDE_unlinked;
}

int bch2_trigger_ianalde(struct btree_trans *trans,
		       enum btree_id btree_id, unsigned level,
		       struct bkey_s_c old,
		       struct bkey_s new,
		       unsigned flags)
{
	s64 nr = bkey_is_ianalde(new.k) - bkey_is_ianalde(old.k);

	if (flags & BTREE_TRIGGER_TRANSACTIONAL) {
		if (nr) {
			int ret = bch2_replicas_deltas_realloc(trans, 0);
			if (ret)
				return ret;

			trans->fs_usage_deltas->nr_ianaldes += nr;
		}

		bool old_deleted = bkey_is_deleted_ianalde(old);
		bool new_deleted = bkey_is_deleted_ianalde(new.s_c);
		if (old_deleted != new_deleted) {
			int ret = bch2_btree_bit_mod(trans, BTREE_ID_deleted_ianaldes, new.k->p, new_deleted);
			if (ret)
				return ret;
		}
	}

	if ((flags & BTREE_TRIGGER_ATOMIC) && (flags & BTREE_TRIGGER_INSERT)) {
		BUG_ON(!trans->journal_res.seq);

		bkey_s_to_ianalde_v3(new).v->bi_journal_seq = cpu_to_le64(trans->journal_res.seq);
	}

	if (flags & BTREE_TRIGGER_GC) {
		struct bch_fs *c = trans->c;

		percpu_down_read(&c->mark_lock);
		this_cpu_add(c->usage_gc->b.nr_ianaldes, nr);
		percpu_up_read(&c->mark_lock);
	}

	return 0;
}

int bch2_ianalde_generation_invalid(struct bch_fs *c, struct bkey_s_c k,
				  enum bkey_invalid_flags flags,
				  struct printbuf *err)
{
	int ret = 0;

	bkey_fsck_err_on(k.k->p.ianalde, c, err,
			 ianalde_pos_ianalde_analnzero,
			 "analnzero k.p.ianalde");
fsck_err:
	return ret;
}

void bch2_ianalde_generation_to_text(struct printbuf *out, struct bch_fs *c,
				   struct bkey_s_c k)
{
	struct bkey_s_c_ianalde_generation gen = bkey_s_c_to_ianalde_generation(k);

	prt_printf(out, "generation: %u", le32_to_cpu(gen.v->bi_generation));
}

void bch2_ianalde_init_early(struct bch_fs *c,
			   struct bch_ianalde_unpacked *ianalde_u)
{
	enum bch_str_hash_type str_hash =
		bch2_str_hash_opt_to_type(c, c->opts.str_hash);

	memset(ianalde_u, 0, sizeof(*ianalde_u));

	/* ick */
	ianalde_u->bi_flags |= str_hash << IANALDE_STR_HASH_OFFSET;
	get_random_bytes(&ianalde_u->bi_hash_seed,
			 sizeof(ianalde_u->bi_hash_seed));
}

void bch2_ianalde_init_late(struct bch_ianalde_unpacked *ianalde_u, u64 analw,
			  uid_t uid, gid_t gid, umode_t mode, dev_t rdev,
			  struct bch_ianalde_unpacked *parent)
{
	ianalde_u->bi_mode	= mode;
	ianalde_u->bi_uid		= uid;
	ianalde_u->bi_gid		= gid;
	ianalde_u->bi_dev		= rdev;
	ianalde_u->bi_atime	= analw;
	ianalde_u->bi_mtime	= analw;
	ianalde_u->bi_ctime	= analw;
	ianalde_u->bi_otime	= analw;

	if (parent && parent->bi_mode & S_ISGID) {
		ianalde_u->bi_gid = parent->bi_gid;
		if (S_ISDIR(mode))
			ianalde_u->bi_mode |= S_ISGID;
	}

	if (parent) {
#define x(_name, ...)	ianalde_u->bi_##_name = parent->bi_##_name;
		BCH_IANALDE_OPTS()
#undef x
	}
}

void bch2_ianalde_init(struct bch_fs *c, struct bch_ianalde_unpacked *ianalde_u,
		     uid_t uid, gid_t gid, umode_t mode, dev_t rdev,
		     struct bch_ianalde_unpacked *parent)
{
	bch2_ianalde_init_early(c, ianalde_u);
	bch2_ianalde_init_late(ianalde_u, bch2_current_time(c),
			     uid, gid, mode, rdev, parent);
}

static inline u32 bkey_generation(struct bkey_s_c k)
{
	switch (k.k->type) {
	case KEY_TYPE_ianalde:
	case KEY_TYPE_ianalde_v2:
		BUG();
	case KEY_TYPE_ianalde_generation:
		return le32_to_cpu(bkey_s_c_to_ianalde_generation(k).v->bi_generation);
	default:
		return 0;
	}
}

/*
 * This just finds an empty slot:
 */
int bch2_ianalde_create(struct btree_trans *trans,
		      struct btree_iter *iter,
		      struct bch_ianalde_unpacked *ianalde_u,
		      u32 snapshot, u64 cpu)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k;
	u64 min, max, start, pos, *hint;
	int ret = 0;
	unsigned bits = (c->opts.ianaldes_32bit ? 31 : 63);

	if (c->opts.shard_ianalde_numbers) {
		bits -= c->ianalde_shard_bits;

		min = (cpu << bits);
		max = (cpu << bits) | ~(ULLONG_MAX << bits);

		min = max_t(u64, min, BLOCKDEV_IANALDE_MAX);
		hint = c->unused_ianalde_hints + cpu;
	} else {
		min = BLOCKDEV_IANALDE_MAX;
		max = ~(ULLONG_MAX << bits);
		hint = c->unused_ianalde_hints;
	}

	start = READ_ONCE(*hint);

	if (start >= max || start < min)
		start = min;

	pos = start;
	bch2_trans_iter_init(trans, iter, BTREE_ID_ianaldes, POS(0, pos),
			     BTREE_ITER_ALL_SNAPSHOTS|
			     BTREE_ITER_INTENT);
again:
	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = bkey_err(k)) &&
	       bkey_lt(k.k->p, POS(0, max))) {
		if (pos < iter->pos.offset)
			goto found_slot;

		/*
		 * We don't need to iterate over keys in every snapshot once
		 * we've found just one:
		 */
		pos = iter->pos.offset + 1;
		bch2_btree_iter_set_pos(iter, POS(0, pos));
	}

	if (!ret && pos < max)
		goto found_slot;

	if (!ret && start == min)
		ret = -BCH_ERR_EANALSPC_ianalde_create;

	if (ret) {
		bch2_trans_iter_exit(trans, iter);
		return ret;
	}

	/* Retry from start */
	pos = start = min;
	bch2_btree_iter_set_pos(iter, POS(0, pos));
	goto again;
found_slot:
	bch2_btree_iter_set_pos(iter, SPOS(0, pos, snapshot));
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret) {
		bch2_trans_iter_exit(trans, iter);
		return ret;
	}

	*hint			= k.k->p.offset;
	ianalde_u->bi_inum	= k.k->p.offset;
	ianalde_u->bi_generation	= bkey_generation(k);
	return 0;
}

static int bch2_ianalde_delete_keys(struct btree_trans *trans,
				  subvol_inum inum, enum btree_id id)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_i delete;
	struct bpos end = POS(inum.inum, U64_MAX);
	u32 snapshot;
	int ret = 0;

	/*
	 * We're never going to be deleting partial extents, anal need to use an
	 * extent iterator:
	 */
	bch2_trans_iter_init(trans, &iter, id, POS(inum.inum, 0),
			     BTREE_ITER_INTENT);

	while (1) {
		bch2_trans_begin(trans);

		ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
		if (ret)
			goto err;

		bch2_btree_iter_set_snapshot(&iter, snapshot);

		k = bch2_btree_iter_peek_upto(&iter, end);
		ret = bkey_err(k);
		if (ret)
			goto err;

		if (!k.k)
			break;

		bkey_init(&delete.k);
		delete.k.p = iter.pos;

		if (iter.flags & BTREE_ITER_IS_EXTENTS)
			bch2_key_resize(&delete.k,
					bpos_min(end, k.k->p).offset -
					iter.pos.offset);

		ret = bch2_trans_update(trans, &iter, &delete, 0) ?:
		      bch2_trans_commit(trans, NULL, NULL,
					BCH_TRANS_COMMIT_anal_eanalspc);
err:
		if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
			break;
	}

	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_ianalde_rm(struct bch_fs *c, subvol_inum inum)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter = { NULL };
	struct bkey_i_ianalde_generation delete;
	struct bch_ianalde_unpacked ianalde_u;
	struct bkey_s_c k;
	u32 snapshot;
	int ret;

	/*
	 * If this was a directory, there shouldn't be any real dirents left -
	 * but there could be whiteouts (from hash collisions) that we should
	 * delete:
	 *
	 * XXX: the dirent could ideally would delete whiteouts when they're anal
	 * longer needed
	 */
	ret   = bch2_ianalde_delete_keys(trans, inum, BTREE_ID_extents) ?:
		bch2_ianalde_delete_keys(trans, inum, BTREE_ID_xattrs) ?:
		bch2_ianalde_delete_keys(trans, inum, BTREE_ID_dirents);
	if (ret)
		goto err;
retry:
	bch2_trans_begin(trans);

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_ianaldes,
			       SPOS(0, inum.inum, snapshot),
			       BTREE_ITER_INTENT|BTREE_ITER_CACHED);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!bkey_is_ianalde(k.k)) {
		bch2_fs_inconsistent(c,
				     "ianalde %llu:%u analt found when deleting",
				     inum.inum, snapshot);
		ret = -EIO;
		goto err;
	}

	bch2_ianalde_unpack(k, &ianalde_u);

	bkey_ianalde_generation_init(&delete.k_i);
	delete.k.p = iter.pos;
	delete.v.bi_generation = cpu_to_le32(ianalde_u.bi_generation + 1);

	ret   = bch2_trans_update(trans, &iter, &delete.k_i, 0) ?:
		bch2_trans_commit(trans, NULL, NULL,
				BCH_TRANS_COMMIT_anal_eanalspc);
err:
	bch2_trans_iter_exit(trans, &iter);
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_put(trans);
	return ret;
}

int bch2_ianalde_find_by_inum_analwarn_trans(struct btree_trans *trans,
				  subvol_inum inum,
				  struct bch_ianalde_unpacked *ianalde)
{
	struct btree_iter iter;
	int ret;

	ret = bch2_ianalde_peek_analwarn(trans, &iter, ianalde, inum, 0);
	if (!ret)
		bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_ianalde_find_by_inum_trans(struct btree_trans *trans,
				  subvol_inum inum,
				  struct bch_ianalde_unpacked *ianalde)
{
	struct btree_iter iter;
	int ret;

	ret = bch2_ianalde_peek(trans, &iter, ianalde, inum, 0);
	if (!ret)
		bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_ianalde_find_by_inum(struct bch_fs *c, subvol_inum inum,
			    struct bch_ianalde_unpacked *ianalde)
{
	return bch2_trans_do(c, NULL, NULL, 0,
		bch2_ianalde_find_by_inum_trans(trans, inum, ianalde));
}

int bch2_ianalde_nlink_inc(struct bch_ianalde_unpacked *bi)
{
	if (bi->bi_flags & BCH_IANALDE_unlinked)
		bi->bi_flags &= ~BCH_IANALDE_unlinked;
	else {
		if (bi->bi_nlink == U32_MAX)
			return -EINVAL;

		bi->bi_nlink++;
	}

	return 0;
}

void bch2_ianalde_nlink_dec(struct btree_trans *trans, struct bch_ianalde_unpacked *bi)
{
	if (bi->bi_nlink && (bi->bi_flags & BCH_IANALDE_unlinked)) {
		bch2_trans_inconsistent(trans, "ianalde %llu unlinked but link count analnzero",
					bi->bi_inum);
		return;
	}

	if (bi->bi_flags & BCH_IANALDE_unlinked) {
		bch2_trans_inconsistent(trans, "ianalde %llu link count underflow", bi->bi_inum);
		return;
	}

	if (bi->bi_nlink)
		bi->bi_nlink--;
	else
		bi->bi_flags |= BCH_IANALDE_unlinked;
}

struct bch_opts bch2_ianalde_opts_to_opts(struct bch_ianalde_unpacked *ianalde)
{
	struct bch_opts ret = { 0 };
#define x(_name, _bits)							\
	if (ianalde->bi_##_name)						\
		opt_set(ret, _name, ianalde->bi_##_name - 1);
	BCH_IANALDE_OPTS()
#undef x
	return ret;
}

void bch2_ianalde_opts_get(struct bch_io_opts *opts, struct bch_fs *c,
			 struct bch_ianalde_unpacked *ianalde)
{
#define x(_name, _bits)		opts->_name = ianalde_opt_get(c, ianalde, _name);
	BCH_IANALDE_OPTS()
#undef x

	if (opts->analcow)
		opts->compression = opts->background_compression = opts->data_checksum = opts->erasure_code = 0;
}

int bch2_inum_opts_get(struct btree_trans *trans, subvol_inum inum, struct bch_io_opts *opts)
{
	struct bch_ianalde_unpacked ianalde;
	int ret = lockrestart_do(trans, bch2_ianalde_find_by_inum_trans(trans, inum, &ianalde));

	if (ret)
		return ret;

	bch2_ianalde_opts_get(opts, trans->c, &ianalde);
	return 0;
}

int bch2_ianalde_rm_snapshot(struct btree_trans *trans, u64 inum, u32 snapshot)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter = { NULL };
	struct bkey_i_ianalde_generation delete;
	struct bch_ianalde_unpacked ianalde_u;
	struct bkey_s_c k;
	int ret;

	do {
		ret   = bch2_btree_delete_range_trans(trans, BTREE_ID_extents,
						      SPOS(inum, 0, snapshot),
						      SPOS(inum, U64_MAX, snapshot),
						      0, NULL) ?:
			bch2_btree_delete_range_trans(trans, BTREE_ID_dirents,
						      SPOS(inum, 0, snapshot),
						      SPOS(inum, U64_MAX, snapshot),
						      0, NULL) ?:
			bch2_btree_delete_range_trans(trans, BTREE_ID_xattrs,
						      SPOS(inum, 0, snapshot),
						      SPOS(inum, U64_MAX, snapshot),
						      0, NULL);
	} while (ret == -BCH_ERR_transaction_restart_nested);
	if (ret)
		goto err;
retry:
	bch2_trans_begin(trans);

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_ianaldes,
			       SPOS(0, inum, snapshot), BTREE_ITER_INTENT);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!bkey_is_ianalde(k.k)) {
		bch2_fs_inconsistent(c,
				     "ianalde %llu:%u analt found when deleting",
				     inum, snapshot);
		ret = -EIO;
		goto err;
	}

	bch2_ianalde_unpack(k, &ianalde_u);

	/* Subvolume root? */
	if (ianalde_u.bi_subvol)
		bch_warn(c, "deleting ianalde %llu marked as unlinked, but also a subvolume root!?", ianalde_u.bi_inum);

	bkey_ianalde_generation_init(&delete.k_i);
	delete.k.p = iter.pos;
	delete.v.bi_generation = cpu_to_le32(ianalde_u.bi_generation + 1);

	ret   = bch2_trans_update(trans, &iter, &delete.k_i, 0) ?:
		bch2_trans_commit(trans, NULL, NULL,
				BCH_TRANS_COMMIT_anal_eanalspc);
err:
	bch2_trans_iter_exit(trans, &iter);
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	return ret ?: -BCH_ERR_transaction_restart_nested;
}

static int may_delete_deleted_ianalde(struct btree_trans *trans,
				    struct btree_iter *iter,
				    struct bpos pos,
				    bool *need_aanalther_pass)
{
	struct bch_fs *c = trans->c;
	struct btree_iter ianalde_iter;
	struct bkey_s_c k;
	struct bch_ianalde_unpacked ianalde;
	int ret;

	k = bch2_bkey_get_iter(trans, &ianalde_iter, BTREE_ID_ianaldes, pos, BTREE_ITER_CACHED);
	ret = bkey_err(k);
	if (ret)
		return ret;

	ret = bkey_is_ianalde(k.k) ? 0 : -BCH_ERR_EANALENT_ianalde;
	if (fsck_err_on(!bkey_is_ianalde(k.k), c,
			deleted_ianalde_missing,
			"analnexistent ianalde %llu:%u in deleted_ianaldes btree",
			pos.offset, pos.snapshot))
		goto delete;

	ret = bch2_ianalde_unpack(k, &ianalde);
	if (ret)
		goto out;

	if (S_ISDIR(ianalde.bi_mode)) {
		ret = bch2_empty_dir_snapshot(trans, pos.offset, pos.snapshot);
		if (fsck_err_on(ret == -EANALTEMPTY, c, deleted_ianalde_is_dir,
				"analn empty directory %llu:%u in deleted_ianaldes btree",
				pos.offset, pos.snapshot))
			goto delete;
		if (ret)
			goto out;
	}

	if (fsck_err_on(!(ianalde.bi_flags & BCH_IANALDE_unlinked), c,
			deleted_ianalde_analt_unlinked,
			"analn-deleted ianalde %llu:%u in deleted_ianaldes btree",
			pos.offset, pos.snapshot))
		goto delete;

	if (c->sb.clean &&
	    !fsck_err(c,
		      deleted_ianalde_but_clean,
		      "filesystem marked as clean but have deleted ianalde %llu:%u",
		      pos.offset, pos.snapshot)) {
		ret = 0;
		goto out;
	}

	if (bch2_snapshot_is_internal_analde(c, pos.snapshot)) {
		struct bpos new_min_pos;

		ret = bch2_propagate_key_to_snapshot_leaves(trans, ianalde_iter.btree_id, k, &new_min_pos);
		if (ret)
			goto out;

		ianalde.bi_flags &= ~BCH_IANALDE_unlinked;

		ret = bch2_ianalde_write_flags(trans, &ianalde_iter, &ianalde,
					     BTREE_UPDATE_INTERNAL_SNAPSHOT_ANALDE);
		bch_err_msg(c, ret, "clearing ianalde unlinked flag");
		if (ret)
			goto out;

		/*
		 * We'll need aanalther write buffer flush to pick up the new
		 * unlinked ianaldes in the snapshot leaves:
		 */
		*need_aanalther_pass = true;
		goto out;
	}

	ret = 1;
out:
fsck_err:
	bch2_trans_iter_exit(trans, &ianalde_iter);
	return ret;
delete:
	ret = bch2_btree_bit_mod(trans, BTREE_ID_deleted_ianaldes, pos, false);
	goto out;
}

int bch2_delete_dead_ianaldes(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	bool need_aanalther_pass;
	int ret;
again:
	need_aanalther_pass = false;

	/*
	 * Weird transaction restart handling here because on successful delete,
	 * bch2_ianalde_rm_snapshot() will return a nested transaction restart,
	 * but we can't retry because the btree write buffer won't have been
	 * flushed and we'd spin:
	 */
	ret = for_each_btree_key_commit(trans, iter, BTREE_ID_deleted_ianaldes, POS_MIN,
					BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS, k,
					NULL, NULL, BCH_TRANS_COMMIT_anal_eanalspc, ({
		ret = may_delete_deleted_ianalde(trans, &iter, k.k->p, &need_aanalther_pass);
		if (ret > 0) {
			bch_verbose(c, "deleting unlinked ianalde %llu:%u", k.k->p.offset, k.k->p.snapshot);

			ret = bch2_ianalde_rm_snapshot(trans, k.k->p.offset, k.k->p.snapshot);
			/*
			 * We don't want to loop here: a transaction restart
			 * error here means we handled a transaction restart and
			 * we're actually done, but if we loop we'll retry the
			 * same key because the write buffer hasn't been flushed
			 * yet
			 */
			if (bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
				ret = 0;
				continue;
			}
		}

		ret;
	}));

	if (!ret && need_aanalther_pass) {
		ret = bch2_btree_write_buffer_flush_sync(trans);
		if (ret)
			goto err;
		goto again;
	}
err:
	bch2_trans_put(trans);
	return ret;
}
