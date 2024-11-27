// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_key_cache.h"
#include "btree_write_buffer.h"
#include "bkey_methods.h"
#include "btree_update.h"
#include "buckets.h"
#include "compress.h"
#include "dirent.h"
#include "disk_accounting.h"
#include "error.h"
#include "extents.h"
#include "extent_update.h"
#include "fs.h"
#include "inode.h"
#include "opts.h"
#include "str_hash.h"
#include "snapshot.h"
#include "subvolume.h"
#include "varint.h"

#include <linux/random.h>

#include <linux/unaligned.h>

#define x(name, ...)	#name,
const char * const bch2_inode_opts[] = {
	BCH_INODE_OPTS()
	NULL,
};

static const char * const bch2_inode_flag_strs[] = {
	BCH_INODE_FLAGS()
	NULL
};
#undef  x

static int delete_ancestor_snapshot_inodes(struct btree_trans *, struct bpos);

static const u8 byte_table[8] = { 1, 2, 3, 4, 6, 8, 10, 13 };

static int inode_decode_field(const u8 *in, const u8 *end,
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

static inline void bch2_inode_pack_inlined(struct bkey_inode_buf *packed,
					   const struct bch_inode_unpacked *inode)
{
	struct bkey_i_inode_v3 *k = &packed->inode;
	u8 *out = k->v.fields;
	u8 *end = (void *) &packed[1];
	u8 *last_nonzero_field = out;
	unsigned nr_fields = 0, last_nonzero_fieldnr = 0;
	unsigned bytes;
	int ret;

	bkey_inode_v3_init(&packed->inode.k_i);
	packed->inode.k.p.offset	= inode->bi_inum;
	packed->inode.v.bi_journal_seq	= cpu_to_le64(inode->bi_journal_seq);
	packed->inode.v.bi_hash_seed	= inode->bi_hash_seed;
	packed->inode.v.bi_flags	= cpu_to_le64(inode->bi_flags);
	packed->inode.v.bi_sectors	= cpu_to_le64(inode->bi_sectors);
	packed->inode.v.bi_size		= cpu_to_le64(inode->bi_size);
	packed->inode.v.bi_version	= cpu_to_le64(inode->bi_version);
	SET_INODEv3_MODE(&packed->inode.v, inode->bi_mode);
	SET_INODEv3_FIELDS_START(&packed->inode.v, INODEv3_FIELDS_START_CUR);


#define x(_name, _bits)							\
	nr_fields++;							\
									\
	if (inode->_name) {						\
		ret = bch2_varint_encode_fast(out, inode->_name);	\
		out += ret;						\
									\
		if (_bits > 64)						\
			*out++ = 0;					\
									\
		last_nonzero_field = out;				\
		last_nonzero_fieldnr = nr_fields;			\
	} else {							\
		*out++ = 0;						\
									\
		if (_bits > 64)						\
			*out++ = 0;					\
	}

	BCH_INODE_FIELDS_v3()
#undef  x
	BUG_ON(out > end);

	out = last_nonzero_field;
	nr_fields = last_nonzero_fieldnr;

	bytes = out - (u8 *) &packed->inode.v;
	set_bkey_val_bytes(&packed->inode.k, bytes);
	memset_u64s_tail(&packed->inode.v, 0, bytes);

	SET_INODEv3_NR_FIELDS(&k->v, nr_fields);

	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG)) {
		struct bch_inode_unpacked unpacked;

		ret = bch2_inode_unpack(bkey_i_to_s_c(&packed->inode.k_i), &unpacked);
		BUG_ON(ret);
		BUG_ON(unpacked.bi_inum		!= inode->bi_inum);
		BUG_ON(unpacked.bi_hash_seed	!= inode->bi_hash_seed);
		BUG_ON(unpacked.bi_sectors	!= inode->bi_sectors);
		BUG_ON(unpacked.bi_size		!= inode->bi_size);
		BUG_ON(unpacked.bi_version	!= inode->bi_version);
		BUG_ON(unpacked.bi_mode		!= inode->bi_mode);

#define x(_name, _bits)	if (unpacked._name != inode->_name)		\
			panic("unpacked %llu should be %llu",		\
			      (u64) unpacked._name, (u64) inode->_name);
		BCH_INODE_FIELDS_v3()
#undef  x
	}
}

void bch2_inode_pack(struct bkey_inode_buf *packed,
		     const struct bch_inode_unpacked *inode)
{
	bch2_inode_pack_inlined(packed, inode);
}

static noinline int bch2_inode_unpack_v1(struct bkey_s_c_inode inode,
				struct bch_inode_unpacked *unpacked)
{
	const u8 *in = inode.v->fields;
	const u8 *end = bkey_val_end(inode);
	u64 field[2];
	unsigned fieldnr = 0, field_bits;
	int ret;

#define x(_name, _bits)							\
	if (fieldnr++ == INODEv1_NR_FIELDS(inode.v)) {			\
		unsigned offset = offsetof(struct bch_inode_unpacked, _name);\
		memset((void *) unpacked + offset, 0,			\
		       sizeof(*unpacked) - offset);			\
		return 0;						\
	}								\
									\
	ret = inode_decode_field(in, end, field, &field_bits);		\
	if (ret < 0)							\
		return ret;						\
									\
	if (field_bits > sizeof(unpacked->_name) * 8)			\
		return -1;						\
									\
	unpacked->_name = field[1];					\
	in += ret;

	BCH_INODE_FIELDS_v2()
#undef  x

	/* XXX: signal if there were more fields than expected? */
	return 0;
}

static int bch2_inode_unpack_v2(struct bch_inode_unpacked *unpacked,
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

	BCH_INODE_FIELDS_v2()
#undef  x

	/* XXX: signal if there were more fields than expected? */
	return 0;
}

static int bch2_inode_unpack_v3(struct bkey_s_c k,
				struct bch_inode_unpacked *unpacked)
{
	struct bkey_s_c_inode_v3 inode = bkey_s_c_to_inode_v3(k);
	const u8 *in = inode.v->fields;
	const u8 *end = bkey_val_end(inode);
	unsigned nr_fields = INODEv3_NR_FIELDS(inode.v);
	unsigned fieldnr = 0;
	int ret;
	u64 v[2];

	unpacked->bi_inum	= inode.k->p.offset;
	unpacked->bi_journal_seq= le64_to_cpu(inode.v->bi_journal_seq);
	unpacked->bi_hash_seed	= inode.v->bi_hash_seed;
	unpacked->bi_flags	= le64_to_cpu(inode.v->bi_flags);
	unpacked->bi_sectors	= le64_to_cpu(inode.v->bi_sectors);
	unpacked->bi_size	= le64_to_cpu(inode.v->bi_size);
	unpacked->bi_version	= le64_to_cpu(inode.v->bi_version);
	unpacked->bi_mode	= INODEv3_MODE(inode.v);

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

	BCH_INODE_FIELDS_v3()
#undef  x

	/* XXX: signal if there were more fields than expected? */
	return 0;
}

static noinline int bch2_inode_unpack_slowpath(struct bkey_s_c k,
					       struct bch_inode_unpacked *unpacked)
{
	memset(unpacked, 0, sizeof(*unpacked));

	unpacked->bi_snapshot = k.k->p.snapshot;

	switch (k.k->type) {
	case KEY_TYPE_inode: {
		struct bkey_s_c_inode inode = bkey_s_c_to_inode(k);

		unpacked->bi_inum	= inode.k->p.offset;
		unpacked->bi_journal_seq= 0;
		unpacked->bi_hash_seed	= inode.v->bi_hash_seed;
		unpacked->bi_flags	= le32_to_cpu(inode.v->bi_flags);
		unpacked->bi_mode	= le16_to_cpu(inode.v->bi_mode);

		if (INODEv1_NEW_VARINT(inode.v)) {
			return bch2_inode_unpack_v2(unpacked, inode.v->fields,
						    bkey_val_end(inode),
						    INODEv1_NR_FIELDS(inode.v));
		} else {
			return bch2_inode_unpack_v1(inode, unpacked);
		}
		break;
	}
	case KEY_TYPE_inode_v2: {
		struct bkey_s_c_inode_v2 inode = bkey_s_c_to_inode_v2(k);

		unpacked->bi_inum	= inode.k->p.offset;
		unpacked->bi_journal_seq= le64_to_cpu(inode.v->bi_journal_seq);
		unpacked->bi_hash_seed	= inode.v->bi_hash_seed;
		unpacked->bi_flags	= le64_to_cpu(inode.v->bi_flags);
		unpacked->bi_mode	= le16_to_cpu(inode.v->bi_mode);

		return bch2_inode_unpack_v2(unpacked, inode.v->fields,
					    bkey_val_end(inode),
					    INODEv2_NR_FIELDS(inode.v));
	}
	default:
		BUG();
	}
}

int bch2_inode_unpack(struct bkey_s_c k,
		      struct bch_inode_unpacked *unpacked)
{
	unpacked->bi_snapshot = k.k->p.snapshot;

	return likely(k.k->type == KEY_TYPE_inode_v3)
		? bch2_inode_unpack_v3(k, unpacked)
		: bch2_inode_unpack_slowpath(k, unpacked);
}

int __bch2_inode_peek(struct btree_trans *trans,
		      struct btree_iter *iter,
		      struct bch_inode_unpacked *inode,
		      subvol_inum inum, unsigned flags,
		      bool warn)
{
	u32 snapshot;
	int ret = __bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot, warn);
	if (ret)
		return ret;

	struct bkey_s_c k = bch2_bkey_get_iter(trans, iter, BTREE_ID_inodes,
					       SPOS(0, inum.inum, snapshot),
					       flags|BTREE_ITER_cached);
	ret = bkey_err(k);
	if (ret)
		return ret;

	ret = bkey_is_inode(k.k) ? 0 : -BCH_ERR_ENOENT_inode;
	if (ret)
		goto err;

	ret = bch2_inode_unpack(k, inode);
	if (ret)
		goto err;

	return 0;
err:
	if (warn)
		bch_err_msg(trans->c, ret, "looking up inum %llu:%llu:", inum.subvol, inum.inum);
	bch2_trans_iter_exit(trans, iter);
	return ret;
}

int bch2_inode_write_flags(struct btree_trans *trans,
		     struct btree_iter *iter,
		     struct bch_inode_unpacked *inode,
		     enum btree_iter_update_trigger_flags flags)
{
	struct bkey_inode_buf *inode_p;

	inode_p = bch2_trans_kmalloc(trans, sizeof(*inode_p));
	if (IS_ERR(inode_p))
		return PTR_ERR(inode_p);

	bch2_inode_pack_inlined(inode_p, inode);
	inode_p->inode.k.p.snapshot = iter->snapshot;
	return bch2_trans_update(trans, iter, &inode_p->inode.k_i, flags);
}

int __bch2_fsck_write_inode(struct btree_trans *trans, struct bch_inode_unpacked *inode)
{
	struct bkey_inode_buf *inode_p =
		bch2_trans_kmalloc(trans, sizeof(*inode_p));

	if (IS_ERR(inode_p))
		return PTR_ERR(inode_p);

	bch2_inode_pack(inode_p, inode);
	inode_p->inode.k.p.snapshot = inode->bi_snapshot;

	return bch2_btree_insert_nonextent(trans, BTREE_ID_inodes,
				&inode_p->inode.k_i,
				BTREE_UPDATE_internal_snapshot_node);
}

int bch2_fsck_write_inode(struct btree_trans *trans, struct bch_inode_unpacked *inode)
{
	int ret = commit_do(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			    __bch2_fsck_write_inode(trans, inode));
	bch_err_fn(trans->c, ret);
	return ret;
}

struct bkey_i *bch2_inode_to_v3(struct btree_trans *trans, struct bkey_i *k)
{
	struct bch_inode_unpacked u;
	struct bkey_inode_buf *inode_p;
	int ret;

	if (!bkey_is_inode(&k->k))
		return ERR_PTR(-ENOENT);

	inode_p = bch2_trans_kmalloc(trans, sizeof(*inode_p));
	if (IS_ERR(inode_p))
		return ERR_CAST(inode_p);

	ret = bch2_inode_unpack(bkey_i_to_s_c(k), &u);
	if (ret)
		return ERR_PTR(ret);

	bch2_inode_pack(inode_p, &u);
	return &inode_p->inode.k_i;
}

static int __bch2_inode_validate(struct bch_fs *c, struct bkey_s_c k,
				 struct bkey_validate_context from)
{
	struct bch_inode_unpacked unpacked;
	int ret = 0;

	bkey_fsck_err_on(k.k->p.inode,
			 c, inode_pos_inode_nonzero,
			 "nonzero k.p.inode");

	bkey_fsck_err_on(k.k->p.offset < BLOCKDEV_INODE_MAX,
			 c, inode_pos_blockdev_range,
			 "fs inode in blockdev range");

	bkey_fsck_err_on(bch2_inode_unpack(k, &unpacked),
			 c, inode_unpack_error,
			 "invalid variable length fields");

	bkey_fsck_err_on(unpacked.bi_data_checksum >= BCH_CSUM_OPT_NR + 1,
			 c, inode_checksum_type_invalid,
			 "invalid data checksum type (%u >= %u",
			 unpacked.bi_data_checksum, BCH_CSUM_OPT_NR + 1);

	bkey_fsck_err_on(unpacked.bi_compression &&
			 !bch2_compression_opt_valid(unpacked.bi_compression - 1),
			 c, inode_compression_type_invalid,
			 "invalid compression opt %u", unpacked.bi_compression - 1);

	bkey_fsck_err_on((unpacked.bi_flags & BCH_INODE_unlinked) &&
			 unpacked.bi_nlink != 0,
			 c, inode_unlinked_but_nlink_nonzero,
			 "flagged as unlinked but bi_nlink != 0");

	bkey_fsck_err_on(unpacked.bi_subvol && !S_ISDIR(unpacked.bi_mode),
			 c, inode_subvol_root_but_not_dir,
			 "subvolume root but not a directory");
fsck_err:
	return ret;
}

int bch2_inode_validate(struct bch_fs *c, struct bkey_s_c k,
			struct bkey_validate_context from)
{
	struct bkey_s_c_inode inode = bkey_s_c_to_inode(k);
	int ret = 0;

	bkey_fsck_err_on(INODEv1_STR_HASH(inode.v) >= BCH_STR_HASH_NR,
			 c, inode_str_hash_invalid,
			 "invalid str hash type (%llu >= %u)",
			 INODEv1_STR_HASH(inode.v), BCH_STR_HASH_NR);

	ret = __bch2_inode_validate(c, k, from);
fsck_err:
	return ret;
}

int bch2_inode_v2_validate(struct bch_fs *c, struct bkey_s_c k,
			   struct bkey_validate_context from)
{
	struct bkey_s_c_inode_v2 inode = bkey_s_c_to_inode_v2(k);
	int ret = 0;

	bkey_fsck_err_on(INODEv2_STR_HASH(inode.v) >= BCH_STR_HASH_NR,
			 c, inode_str_hash_invalid,
			 "invalid str hash type (%llu >= %u)",
			 INODEv2_STR_HASH(inode.v), BCH_STR_HASH_NR);

	ret = __bch2_inode_validate(c, k, from);
fsck_err:
	return ret;
}

int bch2_inode_v3_validate(struct bch_fs *c, struct bkey_s_c k,
			   struct bkey_validate_context from)
{
	struct bkey_s_c_inode_v3 inode = bkey_s_c_to_inode_v3(k);
	int ret = 0;

	bkey_fsck_err_on(INODEv3_FIELDS_START(inode.v) < INODEv3_FIELDS_START_INITIAL ||
			 INODEv3_FIELDS_START(inode.v) > bkey_val_u64s(inode.k),
			 c, inode_v3_fields_start_bad,
			 "invalid fields_start (got %llu, min %u max %zu)",
			 INODEv3_FIELDS_START(inode.v),
			 INODEv3_FIELDS_START_INITIAL,
			 bkey_val_u64s(inode.k));

	bkey_fsck_err_on(INODEv3_STR_HASH(inode.v) >= BCH_STR_HASH_NR,
			 c, inode_str_hash_invalid,
			 "invalid str hash type (%llu >= %u)",
			 INODEv3_STR_HASH(inode.v), BCH_STR_HASH_NR);

	ret = __bch2_inode_validate(c, k, from);
fsck_err:
	return ret;
}

static void __bch2_inode_unpacked_to_text(struct printbuf *out,
					  struct bch_inode_unpacked *inode)
{
	prt_printf(out, "\n");
	printbuf_indent_add(out, 2);
	prt_printf(out, "mode=%o\n", inode->bi_mode);

	prt_str(out, "flags=");
	prt_bitflags(out, bch2_inode_flag_strs, inode->bi_flags & ((1U << 20) - 1));
	prt_printf(out, "(%x)\n", inode->bi_flags);

	prt_printf(out, "journal_seq=%llu\n",	inode->bi_journal_seq);
	prt_printf(out, "hash_seed=%llx\n",	inode->bi_hash_seed);
	prt_printf(out, "hash_type=");
	bch2_prt_str_hash_type(out, INODE_STR_HASH(inode));
	prt_newline(out);
	prt_printf(out, "bi_size=%llu\n",	inode->bi_size);
	prt_printf(out, "bi_sectors=%llu\n",	inode->bi_sectors);
	prt_printf(out, "bi_version=%llu\n",	inode->bi_version);

#define x(_name, _bits)						\
	prt_printf(out, #_name "=%llu\n", (u64) inode->_name);
	BCH_INODE_FIELDS_v3()
#undef  x

	bch2_printbuf_strip_trailing_newline(out);
	printbuf_indent_sub(out, 2);
}

void bch2_inode_unpacked_to_text(struct printbuf *out, struct bch_inode_unpacked *inode)
{
	prt_printf(out, "inum: %llu:%u ", inode->bi_inum, inode->bi_snapshot);
	__bch2_inode_unpacked_to_text(out, inode);
}

void bch2_inode_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bch_inode_unpacked inode;

	if (bch2_inode_unpack(k, &inode)) {
		prt_printf(out, "(unpack error)");
		return;
	}

	__bch2_inode_unpacked_to_text(out, &inode);
}

static inline u64 bkey_inode_flags(struct bkey_s_c k)
{
	switch (k.k->type) {
	case KEY_TYPE_inode:
		return le32_to_cpu(bkey_s_c_to_inode(k).v->bi_flags);
	case KEY_TYPE_inode_v2:
		return le64_to_cpu(bkey_s_c_to_inode_v2(k).v->bi_flags);
	case KEY_TYPE_inode_v3:
		return le64_to_cpu(bkey_s_c_to_inode_v3(k).v->bi_flags);
	default:
		return 0;
	}
}

static inline void bkey_inode_flags_set(struct bkey_s k, u64 f)
{
	switch (k.k->type) {
	case KEY_TYPE_inode:
		bkey_s_to_inode(k).v->bi_flags = cpu_to_le32(f);
		return;
	case KEY_TYPE_inode_v2:
		bkey_s_to_inode_v2(k).v->bi_flags = cpu_to_le64(f);
		return;
	case KEY_TYPE_inode_v3:
		bkey_s_to_inode_v3(k).v->bi_flags = cpu_to_le64(f);
		return;
	default:
		BUG();
	}
}

static inline bool bkey_is_unlinked_inode(struct bkey_s_c k)
{
	unsigned f = bkey_inode_flags(k) & BCH_INODE_unlinked;

	return (f & BCH_INODE_unlinked) && !(f & BCH_INODE_has_child_snapshot);
}

static struct bkey_s_c
bch2_bkey_get_iter_snapshot_parent(struct btree_trans *trans, struct btree_iter *iter,
				   enum btree_id btree, struct bpos pos,
				   unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k;
	int ret = 0;

	for_each_btree_key_max_norestart(trans, *iter, btree,
					  bpos_successor(pos),
					  SPOS(pos.inode, pos.offset, U32_MAX),
					  flags|BTREE_ITER_all_snapshots, k, ret)
		if (bch2_snapshot_is_ancestor(c, pos.snapshot, k.k->p.snapshot))
			return k;

	bch2_trans_iter_exit(trans, iter);
	return ret ? bkey_s_c_err(ret) : bkey_s_c_null;
}

static struct bkey_s_c
bch2_inode_get_iter_snapshot_parent(struct btree_trans *trans, struct btree_iter *iter,
				    struct bpos pos, unsigned flags)
{
	struct bkey_s_c k;
again:
	k = bch2_bkey_get_iter_snapshot_parent(trans, iter, BTREE_ID_inodes, pos, flags);
	if (!k.k ||
	    bkey_err(k) ||
	    bkey_is_inode(k.k))
		return k;

	bch2_trans_iter_exit(trans, iter);
	pos = k.k->p;
	goto again;
}

int __bch2_inode_has_child_snapshots(struct btree_trans *trans, struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	for_each_btree_key_max_norestart(trans, iter,
			BTREE_ID_inodes, POS(0, pos.offset), bpos_predecessor(pos),
			BTREE_ITER_all_snapshots|
			BTREE_ITER_with_updates, k, ret)
		if (bch2_snapshot_is_ancestor(c, k.k->p.snapshot, pos.snapshot) &&
		    bkey_is_inode(k.k)) {
			ret = 1;
			break;
		}
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int update_inode_has_children(struct btree_trans *trans,
				     struct bkey_s k,
				     bool have_child)
{
	if (!have_child) {
		int ret = bch2_inode_has_child_snapshots(trans, k.k->p);
		if (ret)
			return ret < 0 ? ret : 0;
	}

	u64 f = bkey_inode_flags(k.s_c);
	if (have_child != !!(f & BCH_INODE_has_child_snapshot))
		bkey_inode_flags_set(k, f ^ BCH_INODE_has_child_snapshot);

	return 0;
}

static int update_parent_inode_has_children(struct btree_trans *trans, struct bpos pos,
					    bool have_child)
{
	struct btree_iter iter;
	struct bkey_s_c k = bch2_inode_get_iter_snapshot_parent(trans,
						&iter, pos, BTREE_ITER_with_updates);
	int ret = bkey_err(k);
	if (ret)
		return ret;
	if (!k.k)
		return 0;

	if (!have_child) {
		ret = bch2_inode_has_child_snapshots(trans, k.k->p);
		if (ret) {
			ret = ret < 0 ? ret : 0;
			goto err;
		}
	}

	u64 f = bkey_inode_flags(k);
	if (have_child != !!(f & BCH_INODE_has_child_snapshot)) {
		struct bkey_i *update = bch2_bkey_make_mut(trans, &iter, &k,
					     BTREE_UPDATE_internal_snapshot_node);
		ret = PTR_ERR_OR_ZERO(update);
		if (ret)
			goto err;

		bkey_inode_flags_set(bkey_i_to_s(update), f ^ BCH_INODE_has_child_snapshot);
	}
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_trigger_inode(struct btree_trans *trans,
		       enum btree_id btree_id, unsigned level,
		       struct bkey_s_c old,
		       struct bkey_s new,
		       enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;

	if ((flags & BTREE_TRIGGER_atomic) && (flags & BTREE_TRIGGER_insert)) {
		BUG_ON(!trans->journal_res.seq);
		bkey_s_to_inode_v3(new).v->bi_journal_seq = cpu_to_le64(trans->journal_res.seq);
	}

	s64 nr = bkey_is_inode(new.k) - bkey_is_inode(old.k);
	if ((flags & (BTREE_TRIGGER_transactional|BTREE_TRIGGER_gc)) && nr) {
		struct disk_accounting_pos acc = { .type = BCH_DISK_ACCOUNTING_nr_inodes };
		int ret = bch2_disk_accounting_mod(trans, &acc, &nr, 1, flags & BTREE_TRIGGER_gc);
		if (ret)
			return ret;
	}

	if (flags & BTREE_TRIGGER_transactional) {
		int unlinked_delta =	(int) bkey_is_unlinked_inode(new.s_c) -
					(int) bkey_is_unlinked_inode(old);
		if (unlinked_delta) {
			int ret = bch2_btree_bit_mod_buffered(trans, BTREE_ID_deleted_inodes,
							      new.k->p, unlinked_delta > 0);
			if (ret)
				return ret;
		}

		/*
		 * If we're creating or deleting an inode at this snapshot ID,
		 * and there might be an inode in a parent snapshot ID, we might
		 * need to set or clear the has_child_snapshot flag on the
		 * parent.
		 */
		int deleted_delta = (int) bkey_is_inode(new.k) -
				    (int) bkey_is_inode(old.k);
		if (deleted_delta &&
		    bch2_snapshot_parent(c, new.k->p.snapshot)) {
			int ret = update_parent_inode_has_children(trans, new.k->p,
								   deleted_delta > 0);
			if (ret)
				return ret;
		}

		/*
		 * When an inode is first updated in a new snapshot, we may need
		 * to clear has_child_snapshot
		 */
		if (deleted_delta > 0) {
			int ret = update_inode_has_children(trans, new, false);
			if (ret)
				return ret;
		}
	}

	return 0;
}

int bch2_inode_generation_validate(struct bch_fs *c, struct bkey_s_c k,
				   struct bkey_validate_context from)
{
	int ret = 0;

	bkey_fsck_err_on(k.k->p.inode,
			 c, inode_pos_inode_nonzero,
			 "nonzero k.p.inode");
fsck_err:
	return ret;
}

void bch2_inode_generation_to_text(struct printbuf *out, struct bch_fs *c,
				   struct bkey_s_c k)
{
	struct bkey_s_c_inode_generation gen = bkey_s_c_to_inode_generation(k);

	prt_printf(out, "generation: %u", le32_to_cpu(gen.v->bi_generation));
}

void bch2_inode_init_early(struct bch_fs *c,
			   struct bch_inode_unpacked *inode_u)
{
	enum bch_str_hash_type str_hash =
		bch2_str_hash_opt_to_type(c, c->opts.str_hash);

	memset(inode_u, 0, sizeof(*inode_u));

	SET_INODE_STR_HASH(inode_u, str_hash);
	get_random_bytes(&inode_u->bi_hash_seed, sizeof(inode_u->bi_hash_seed));
}

void bch2_inode_init_late(struct bch_inode_unpacked *inode_u, u64 now,
			  uid_t uid, gid_t gid, umode_t mode, dev_t rdev,
			  struct bch_inode_unpacked *parent)
{
	inode_u->bi_mode	= mode;
	inode_u->bi_uid		= uid;
	inode_u->bi_gid		= gid;
	inode_u->bi_dev		= rdev;
	inode_u->bi_atime	= now;
	inode_u->bi_mtime	= now;
	inode_u->bi_ctime	= now;
	inode_u->bi_otime	= now;

	if (parent && parent->bi_mode & S_ISGID) {
		inode_u->bi_gid = parent->bi_gid;
		if (S_ISDIR(mode))
			inode_u->bi_mode |= S_ISGID;
	}

	if (parent) {
#define x(_name, ...)	inode_u->bi_##_name = parent->bi_##_name;
		BCH_INODE_OPTS()
#undef x
	}
}

void bch2_inode_init(struct bch_fs *c, struct bch_inode_unpacked *inode_u,
		     uid_t uid, gid_t gid, umode_t mode, dev_t rdev,
		     struct bch_inode_unpacked *parent)
{
	bch2_inode_init_early(c, inode_u);
	bch2_inode_init_late(inode_u, bch2_current_time(c),
			     uid, gid, mode, rdev, parent);
}

static inline u32 bkey_generation(struct bkey_s_c k)
{
	switch (k.k->type) {
	case KEY_TYPE_inode:
	case KEY_TYPE_inode_v2:
		BUG();
	case KEY_TYPE_inode_generation:
		return le32_to_cpu(bkey_s_c_to_inode_generation(k).v->bi_generation);
	default:
		return 0;
	}
}

/*
 * This just finds an empty slot:
 */
int bch2_inode_create(struct btree_trans *trans,
		      struct btree_iter *iter,
		      struct bch_inode_unpacked *inode_u,
		      u32 snapshot, u64 cpu)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k;
	u64 min, max, start, pos, *hint;
	int ret = 0;
	unsigned bits = (c->opts.inodes_32bit ? 31 : 63);

	if (c->opts.shard_inode_numbers) {
		bits -= c->inode_shard_bits;

		min = (cpu << bits);
		max = (cpu << bits) | ~(ULLONG_MAX << bits);

		min = max_t(u64, min, BLOCKDEV_INODE_MAX);
		hint = c->unused_inode_hints + cpu;
	} else {
		min = BLOCKDEV_INODE_MAX;
		max = ~(ULLONG_MAX << bits);
		hint = c->unused_inode_hints;
	}

	start = READ_ONCE(*hint);

	if (start >= max || start < min)
		start = min;

	pos = start;
	bch2_trans_iter_init(trans, iter, BTREE_ID_inodes, POS(0, pos),
			     BTREE_ITER_all_snapshots|
			     BTREE_ITER_intent);
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
		ret = -BCH_ERR_ENOSPC_inode_create;

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
	inode_u->bi_inum	= k.k->p.offset;
	inode_u->bi_generation	= bkey_generation(k);
	return 0;
}

static int bch2_inode_delete_keys(struct btree_trans *trans,
				  subvol_inum inum, enum btree_id id)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_i delete;
	struct bpos end = POS(inum.inum, U64_MAX);
	u32 snapshot;
	int ret = 0;

	/*
	 * We're never going to be deleting partial extents, no need to use an
	 * extent iterator:
	 */
	bch2_trans_iter_init(trans, &iter, id, POS(inum.inum, 0),
			     BTREE_ITER_intent);

	while (1) {
		bch2_trans_begin(trans);

		ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
		if (ret)
			goto err;

		bch2_btree_iter_set_snapshot(&iter, snapshot);

		k = bch2_btree_iter_peek_max(&iter, end);
		ret = bkey_err(k);
		if (ret)
			goto err;

		if (!k.k)
			break;

		bkey_init(&delete.k);
		delete.k.p = iter.pos;

		if (iter.flags & BTREE_ITER_is_extents)
			bch2_key_resize(&delete.k,
					bpos_min(end, k.k->p).offset -
					iter.pos.offset);

		ret = bch2_trans_update(trans, &iter, &delete, 0) ?:
		      bch2_trans_commit(trans, NULL, NULL,
					BCH_TRANS_COMMIT_no_enospc);
err:
		if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
			break;
	}

	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_inode_rm(struct bch_fs *c, subvol_inum inum)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter = { NULL };
	struct bkey_i_inode_generation delete;
	struct bch_inode_unpacked inode_u;
	struct bkey_s_c k;
	u32 snapshot;
	int ret;

	/*
	 * If this was a directory, there shouldn't be any real dirents left -
	 * but there could be whiteouts (from hash collisions) that we should
	 * delete:
	 *
	 * XXX: the dirent could ideally would delete whiteouts when they're no
	 * longer needed
	 */
	ret   = bch2_inode_delete_keys(trans, inum, BTREE_ID_extents) ?:
		bch2_inode_delete_keys(trans, inum, BTREE_ID_xattrs) ?:
		bch2_inode_delete_keys(trans, inum, BTREE_ID_dirents);
	if (ret)
		goto err;
retry:
	bch2_trans_begin(trans);

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_inodes,
			       SPOS(0, inum.inum, snapshot),
			       BTREE_ITER_intent|BTREE_ITER_cached);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!bkey_is_inode(k.k)) {
		bch2_fs_inconsistent(c,
				     "inode %llu:%u not found when deleting",
				     inum.inum, snapshot);
		ret = -EIO;
		goto err;
	}

	bch2_inode_unpack(k, &inode_u);

	bkey_inode_generation_init(&delete.k_i);
	delete.k.p = iter.pos;
	delete.v.bi_generation = cpu_to_le32(inode_u.bi_generation + 1);

	ret   = bch2_trans_update(trans, &iter, &delete.k_i, 0) ?:
		bch2_trans_commit(trans, NULL, NULL,
				BCH_TRANS_COMMIT_no_enospc);
err:
	bch2_trans_iter_exit(trans, &iter);
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	if (ret)
		goto err2;

	ret = delete_ancestor_snapshot_inodes(trans, SPOS(0, inum.inum, snapshot));
err2:
	bch2_trans_put(trans);
	return ret;
}

int bch2_inode_find_by_inum_nowarn_trans(struct btree_trans *trans,
				  subvol_inum inum,
				  struct bch_inode_unpacked *inode)
{
	struct btree_iter iter;
	int ret;

	ret = bch2_inode_peek_nowarn(trans, &iter, inode, inum, 0);
	if (!ret)
		bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_inode_find_by_inum_trans(struct btree_trans *trans,
				  subvol_inum inum,
				  struct bch_inode_unpacked *inode)
{
	struct btree_iter iter;
	int ret;

	ret = bch2_inode_peek(trans, &iter, inode, inum, 0);
	if (!ret)
		bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_inode_find_by_inum(struct bch_fs *c, subvol_inum inum,
			    struct bch_inode_unpacked *inode)
{
	return bch2_trans_do(c, bch2_inode_find_by_inum_trans(trans, inum, inode));
}

int bch2_inode_nlink_inc(struct bch_inode_unpacked *bi)
{
	if (bi->bi_flags & BCH_INODE_unlinked)
		bi->bi_flags &= ~BCH_INODE_unlinked;
	else {
		if (bi->bi_nlink == U32_MAX)
			return -EINVAL;

		bi->bi_nlink++;
	}

	return 0;
}

void bch2_inode_nlink_dec(struct btree_trans *trans, struct bch_inode_unpacked *bi)
{
	if (bi->bi_nlink && (bi->bi_flags & BCH_INODE_unlinked)) {
		bch2_trans_inconsistent(trans, "inode %llu unlinked but link count nonzero",
					bi->bi_inum);
		return;
	}

	if (bi->bi_flags & BCH_INODE_unlinked) {
		bch2_trans_inconsistent(trans, "inode %llu link count underflow", bi->bi_inum);
		return;
	}

	if (bi->bi_nlink)
		bi->bi_nlink--;
	else
		bi->bi_flags |= BCH_INODE_unlinked;
}

struct bch_opts bch2_inode_opts_to_opts(struct bch_inode_unpacked *inode)
{
	struct bch_opts ret = { 0 };
#define x(_name, _bits)							\
	if (inode->bi_##_name)						\
		opt_set(ret, _name, inode->bi_##_name - 1);
	BCH_INODE_OPTS()
#undef x
	return ret;
}

void bch2_inode_opts_get(struct bch_io_opts *opts, struct bch_fs *c,
			 struct bch_inode_unpacked *inode)
{
#define x(_name, _bits)							\
	if ((inode)->bi_##_name) {					\
		opts->_name = inode->bi_##_name - 1;			\
		opts->_name##_from_inode = true;			\
	} else {							\
		opts->_name = c->opts._name;				\
	}
	BCH_INODE_OPTS()
#undef x

	bch2_io_opts_fixups(opts);
}

int bch2_inum_opts_get(struct btree_trans *trans, subvol_inum inum, struct bch_io_opts *opts)
{
	struct bch_inode_unpacked inode;
	int ret = lockrestart_do(trans, bch2_inode_find_by_inum_trans(trans, inum, &inode));

	if (ret)
		return ret;

	bch2_inode_opts_get(opts, trans->c, &inode);
	return 0;
}

static noinline int __bch2_inode_rm_snapshot(struct btree_trans *trans, u64 inum, u32 snapshot)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter = { NULL };
	struct bkey_i_inode_generation delete;
	struct bch_inode_unpacked inode_u;
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

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_inodes,
			       SPOS(0, inum, snapshot), BTREE_ITER_intent);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!bkey_is_inode(k.k)) {
		bch2_fs_inconsistent(c,
				     "inode %llu:%u not found when deleting",
				     inum, snapshot);
		ret = -EIO;
		goto err;
	}

	bch2_inode_unpack(k, &inode_u);

	/* Subvolume root? */
	if (inode_u.bi_subvol)
		bch_warn(c, "deleting inode %llu marked as unlinked, but also a subvolume root!?", inode_u.bi_inum);

	bkey_inode_generation_init(&delete.k_i);
	delete.k.p = iter.pos;
	delete.v.bi_generation = cpu_to_le32(inode_u.bi_generation + 1);

	ret   = bch2_trans_update(trans, &iter, &delete.k_i, 0) ?:
		bch2_trans_commit(trans, NULL, NULL,
				BCH_TRANS_COMMIT_no_enospc);
err:
	bch2_trans_iter_exit(trans, &iter);
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	return ret ?: -BCH_ERR_transaction_restart_nested;
}

/*
 * After deleting an inode, there may be versions in older snapshots that should
 * also be deleted - if they're not referenced by sibling snapshots and not open
 * in other subvolumes:
 */
static int delete_ancestor_snapshot_inodes(struct btree_trans *trans, struct bpos pos)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;
next_parent:
	ret = lockrestart_do(trans,
		bkey_err(k = bch2_inode_get_iter_snapshot_parent(trans, &iter, pos, 0)));
	if (ret || !k.k)
		return ret;

	bool unlinked = bkey_is_unlinked_inode(k);
	pos = k.k->p;
	bch2_trans_iter_exit(trans, &iter);

	if (!unlinked)
		return 0;

	ret = lockrestart_do(trans, bch2_inode_or_descendents_is_open(trans, pos));
	if (ret)
		return ret < 0 ? ret : 0;

	ret = __bch2_inode_rm_snapshot(trans, pos.offset, pos.snapshot);
	if (ret)
		return ret;
	goto next_parent;
}

int bch2_inode_rm_snapshot(struct btree_trans *trans, u64 inum, u32 snapshot)
{
	return __bch2_inode_rm_snapshot(trans, inum, snapshot) ?:
		delete_ancestor_snapshot_inodes(trans, SPOS(0, inum, snapshot));
}

static int may_delete_deleted_inode(struct btree_trans *trans,
				    struct btree_iter *iter,
				    struct bpos pos,
				    bool *need_another_pass)
{
	struct bch_fs *c = trans->c;
	struct btree_iter inode_iter;
	struct bkey_s_c k;
	struct bch_inode_unpacked inode;
	struct printbuf buf = PRINTBUF;
	int ret;

	k = bch2_bkey_get_iter(trans, &inode_iter, BTREE_ID_inodes, pos, BTREE_ITER_cached);
	ret = bkey_err(k);
	if (ret)
		return ret;

	ret = bkey_is_inode(k.k) ? 0 : -BCH_ERR_ENOENT_inode;
	if (fsck_err_on(!bkey_is_inode(k.k),
			trans, deleted_inode_missing,
			"nonexistent inode %llu:%u in deleted_inodes btree",
			pos.offset, pos.snapshot))
		goto delete;

	ret = bch2_inode_unpack(k, &inode);
	if (ret)
		goto out;

	if (S_ISDIR(inode.bi_mode)) {
		ret = bch2_empty_dir_snapshot(trans, pos.offset, 0, pos.snapshot);
		if (fsck_err_on(bch2_err_matches(ret, ENOTEMPTY),
				trans, deleted_inode_is_dir,
				"non empty directory %llu:%u in deleted_inodes btree",
				pos.offset, pos.snapshot))
			goto delete;
		if (ret)
			goto out;
	}

	if (fsck_err_on(!(inode.bi_flags & BCH_INODE_unlinked),
			trans, deleted_inode_not_unlinked,
			"non-deleted inode %llu:%u in deleted_inodes btree",
			pos.offset, pos.snapshot))
		goto delete;

	if (fsck_err_on(inode.bi_flags & BCH_INODE_has_child_snapshot,
			trans, deleted_inode_has_child_snapshots,
			"inode with child snapshots %llu:%u in deleted_inodes btree",
			pos.offset, pos.snapshot))
		goto delete;

	ret = bch2_inode_has_child_snapshots(trans, k.k->p);
	if (ret < 0)
		goto out;

	if (ret) {
		if (fsck_err(trans, inode_has_child_snapshots_wrong,
			     "inode has_child_snapshots flag wrong (should be set)\n%s",
			     (printbuf_reset(&buf),
			      bch2_inode_unpacked_to_text(&buf, &inode),
			      buf.buf))) {
			inode.bi_flags |= BCH_INODE_has_child_snapshot;
			ret = __bch2_fsck_write_inode(trans, &inode);
			if (ret)
				goto out;
		}
		goto delete;

	}

	if (test_bit(BCH_FS_clean_recovery, &c->flags) &&
	    !fsck_err(trans, deleted_inode_but_clean,
		      "filesystem marked as clean but have deleted inode %llu:%u",
		      pos.offset, pos.snapshot)) {
		ret = 0;
		goto out;
	}

	ret = 1;
out:
fsck_err:
	bch2_trans_iter_exit(trans, &inode_iter);
	printbuf_exit(&buf);
	return ret;
delete:
	ret = bch2_btree_bit_mod_buffered(trans, BTREE_ID_deleted_inodes, pos, false);
	goto out;
}

int bch2_delete_dead_inodes(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	bool need_another_pass;
	int ret;
again:
	/*
	 * if we ran check_inodes() unlinked inodes will have already been
	 * cleaned up but the write buffer will be out of sync; therefore we
	 * alway need a write buffer flush
	 */
	ret = bch2_btree_write_buffer_flush_sync(trans);
	if (ret)
		goto err;

	need_another_pass = false;

	/*
	 * Weird transaction restart handling here because on successful delete,
	 * bch2_inode_rm_snapshot() will return a nested transaction restart,
	 * but we can't retry because the btree write buffer won't have been
	 * flushed and we'd spin:
	 */
	ret = for_each_btree_key_commit(trans, iter, BTREE_ID_deleted_inodes, POS_MIN,
					BTREE_ITER_prefetch|BTREE_ITER_all_snapshots, k,
					NULL, NULL, BCH_TRANS_COMMIT_no_enospc, ({
		ret = may_delete_deleted_inode(trans, &iter, k.k->p, &need_another_pass);
		if (ret > 0) {
			bch_verbose_ratelimited(c, "deleting unlinked inode %llu:%u",
						k.k->p.offset, k.k->p.snapshot);

			ret = bch2_inode_rm_snapshot(trans, k.k->p.offset, k.k->p.snapshot);
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

	if (!ret && need_another_pass)
		goto again;
err:
	bch2_trans_put(trans);
	return ret;
}
