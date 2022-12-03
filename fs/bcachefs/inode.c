// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_key_cache.h"
#include "bkey_methods.h"
#include "btree_update.h"
#include "buckets.h"
#include "error.h"
#include "extents.h"
#include "extent_update.h"
#include "inode.h"
#include "str_hash.h"
#include "subvolume.h"
#include "varint.h"

#include <linux/random.h>

#include <asm/unaligned.h>

const char * const bch2_inode_opts[] = {
#define x(name, ...)	#name,
	BCH_INODE_OPTS()
#undef  x
	NULL,
};

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

static inline void bch2_inode_pack_inlined(struct bch_fs *c,
					   struct bkey_inode_buf *packed,
					   const struct bch_inode_unpacked *inode)
{
	struct bkey_i_inode_v2 *k = &packed->inode;
	u8 *out = k->v.fields;
	u8 *end = (void *) &packed[1];
	u8 *last_nonzero_field = out;
	unsigned nr_fields = 0, last_nonzero_fieldnr = 0;
	unsigned bytes;
	int ret;

	bkey_inode_v2_init(&packed->inode.k_i);
	packed->inode.k.p.offset	= inode->bi_inum;
	packed->inode.v.bi_journal_seq	= cpu_to_le64(inode->bi_journal_seq);
	packed->inode.v.bi_hash_seed	= inode->bi_hash_seed;
	packed->inode.v.bi_flags	= cpu_to_le64(inode->bi_flags);
	packed->inode.v.bi_flags	= cpu_to_le64(inode->bi_flags);
	packed->inode.v.bi_mode		= cpu_to_le16(inode->bi_mode);

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

	BCH_INODE_FIELDS()
#undef  x
	BUG_ON(out > end);

	out = last_nonzero_field;
	nr_fields = last_nonzero_fieldnr;

	bytes = out - (u8 *) &packed->inode.v;
	set_bkey_val_bytes(&packed->inode.k, bytes);
	memset_u64s_tail(&packed->inode.v, 0, bytes);

	SET_INODEv2_NR_FIELDS(&k->v, nr_fields);

	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG)) {
		struct bch_inode_unpacked unpacked;

		int ret = bch2_inode_unpack(bkey_i_to_s_c(&packed->inode.k_i),
					   &unpacked);
		BUG_ON(ret);
		BUG_ON(unpacked.bi_inum		!= inode->bi_inum);
		BUG_ON(unpacked.bi_hash_seed	!= inode->bi_hash_seed);
		BUG_ON(unpacked.bi_mode		!= inode->bi_mode);

#define x(_name, _bits)	if (unpacked._name != inode->_name)		\
			panic("unpacked %llu should be %llu",		\
			      (u64) unpacked._name, (u64) inode->_name);
		BCH_INODE_FIELDS()
#undef  x
	}
}

void bch2_inode_pack(struct bch_fs *c,
		     struct bkey_inode_buf *packed,
		     const struct bch_inode_unpacked *inode)
{
	bch2_inode_pack_inlined(c, packed, inode);
}

static noinline int bch2_inode_unpack_v1(struct bkey_s_c_inode inode,
				struct bch_inode_unpacked *unpacked)
{
	const u8 *in = inode.v->fields;
	const u8 *end = bkey_val_end(inode);
	u64 field[2];
	unsigned fieldnr = 0, field_bits;
	int ret;

#define x(_name, _bits)					\
	if (fieldnr++ == INODE_NR_FIELDS(inode.v)) {			\
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

	BCH_INODE_FIELDS()
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

	BCH_INODE_FIELDS()
#undef  x

	/* XXX: signal if there were more fields than expected? */
	return 0;
}

int bch2_inode_unpack(struct bkey_s_c k,
		      struct bch_inode_unpacked *unpacked)
{
	memset(unpacked, 0, sizeof(*unpacked));

	switch (k.k->type) {
	case KEY_TYPE_inode: {
		struct bkey_s_c_inode inode = bkey_s_c_to_inode(k);

		unpacked->bi_inum	= inode.k->p.offset;
		unpacked->bi_journal_seq= 0;
		unpacked->bi_hash_seed	= inode.v->bi_hash_seed;
		unpacked->bi_flags	= le32_to_cpu(inode.v->bi_flags);
		unpacked->bi_mode	= le16_to_cpu(inode.v->bi_mode);

		if (INODE_NEW_VARINT(inode.v)) {
			return bch2_inode_unpack_v2(unpacked, inode.v->fields,
						    bkey_val_end(inode),
						    INODE_NR_FIELDS(inode.v));
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

int bch2_inode_peek(struct btree_trans *trans,
		    struct btree_iter *iter,
		    struct bch_inode_unpacked *inode,
		    subvol_inum inum, unsigned flags)
{
	struct bkey_s_c k;
	u32 snapshot;
	int ret;

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		return ret;

	bch2_trans_iter_init(trans, iter, BTREE_ID_inodes,
			     SPOS(0, inum.inum, snapshot),
			     flags|BTREE_ITER_CACHED);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	ret = bkey_is_inode(k.k) ? 0 : -ENOENT;
	if (ret)
		goto err;

	ret = bch2_inode_unpack(k, inode);
	if (ret)
		goto err;

	return 0;
err:
	bch2_trans_iter_exit(trans, iter);
	return ret;
}

int bch2_inode_write(struct btree_trans *trans,
		     struct btree_iter *iter,
		     struct bch_inode_unpacked *inode)
{
	struct bkey_inode_buf *inode_p;

	inode_p = bch2_trans_kmalloc(trans, sizeof(*inode_p));
	if (IS_ERR(inode_p))
		return PTR_ERR(inode_p);

	bch2_inode_pack_inlined(trans->c, inode_p, inode);
	inode_p->inode.k.p.snapshot = iter->snapshot;
	return bch2_trans_update(trans, iter, &inode_p->inode.k_i, 0);
}

static int __bch2_inode_invalid(struct bkey_s_c k, struct printbuf *err)
{
	struct bch_inode_unpacked unpacked;

	if (k.k->p.inode) {
		prt_printf(err, "nonzero k.p.inode");
		return -BCH_ERR_invalid_bkey;
	}

	if (k.k->p.offset < BLOCKDEV_INODE_MAX) {
		prt_printf(err, "fs inode in blockdev range");
		return -BCH_ERR_invalid_bkey;
	}

	if (bch2_inode_unpack(k, &unpacked)) {
		prt_printf(err, "invalid variable length fields");
		return -BCH_ERR_invalid_bkey;
	}

	if (unpacked.bi_data_checksum >= BCH_CSUM_OPT_NR + 1) {
		prt_printf(err, "invalid data checksum type (%u >= %u",
			unpacked.bi_data_checksum, BCH_CSUM_OPT_NR + 1);
		return -BCH_ERR_invalid_bkey;
	}

	if (unpacked.bi_compression >= BCH_COMPRESSION_OPT_NR + 1) {
		prt_printf(err, "invalid data checksum type (%u >= %u)",
		       unpacked.bi_compression, BCH_COMPRESSION_OPT_NR + 1);
		return -BCH_ERR_invalid_bkey;
	}

	if ((unpacked.bi_flags & BCH_INODE_UNLINKED) &&
	    unpacked.bi_nlink != 0) {
		prt_printf(err, "flagged as unlinked but bi_nlink != 0");
		return -BCH_ERR_invalid_bkey;
	}

	if (unpacked.bi_subvol && !S_ISDIR(unpacked.bi_mode)) {
		prt_printf(err, "subvolume root but not a directory");
		return -BCH_ERR_invalid_bkey;
	}

	return 0;
}

int bch2_inode_invalid(const struct bch_fs *c, struct bkey_s_c k,
		       int rw, struct printbuf *err)
{
	struct bkey_s_c_inode inode = bkey_s_c_to_inode(k);

	if (bkey_val_bytes(k.k) < sizeof(*inode.v)) {
		prt_printf(err, "incorrect value size (%zu < %zu)",
		       bkey_val_bytes(k.k), sizeof(*inode.v));
		return -BCH_ERR_invalid_bkey;
	}

	if (INODE_STR_HASH(inode.v) >= BCH_STR_HASH_NR) {
		prt_printf(err, "invalid str hash type (%llu >= %u)",
		       INODE_STR_HASH(inode.v), BCH_STR_HASH_NR);
		return -BCH_ERR_invalid_bkey;
	}

	return __bch2_inode_invalid(k, err);
}

int bch2_inode_v2_invalid(const struct bch_fs *c, struct bkey_s_c k,
			  int rw, struct printbuf *err)
{
	struct bkey_s_c_inode_v2 inode = bkey_s_c_to_inode_v2(k);

	if (bkey_val_bytes(k.k) < sizeof(*inode.v)) {
		prt_printf(err, "incorrect value size (%zu < %zu)",
		       bkey_val_bytes(k.k), sizeof(*inode.v));
		return -BCH_ERR_invalid_bkey;
	}

	if (INODEv2_STR_HASH(inode.v) >= BCH_STR_HASH_NR) {
		prt_printf(err, "invalid str hash type (%llu >= %u)",
		       INODEv2_STR_HASH(inode.v), BCH_STR_HASH_NR);
		return -BCH_ERR_invalid_bkey;
	}

	return __bch2_inode_invalid(k, err);
}

static void __bch2_inode_unpacked_to_text(struct printbuf *out, struct bch_inode_unpacked *inode)
{
	prt_printf(out, "mode %o flags %x journal_seq %llu",
	       inode->bi_mode, inode->bi_flags,
	       inode->bi_journal_seq);

#define x(_name, _bits)						\
	prt_printf(out, " "#_name " %llu", (u64) inode->_name);
	BCH_INODE_FIELDS()
#undef  x
}

void bch2_inode_unpacked_to_text(struct printbuf *out, struct bch_inode_unpacked *inode)
{
	prt_printf(out, "inum: %llu ", inode->bi_inum);
	__bch2_inode_unpacked_to_text(out, inode);
}

void bch2_inode_to_text(struct printbuf *out, struct bch_fs *c,
		       struct bkey_s_c k)
{
	struct bch_inode_unpacked inode;

	if (bch2_inode_unpack(k, &inode)) {
		prt_printf(out, "(unpack error)");
		return;
	}

	__bch2_inode_unpacked_to_text(out, &inode);
}

int bch2_inode_generation_invalid(const struct bch_fs *c, struct bkey_s_c k,
				  int rw, struct printbuf *err)
{
	if (k.k->p.inode) {
		prt_printf(err, "nonzero k.p.inode");
		return -BCH_ERR_invalid_bkey;
	}

	if (bkey_val_bytes(k.k) != sizeof(struct bch_inode_generation)) {
		prt_printf(err, "incorrect value size (%zu != %zu)",
		       bkey_val_bytes(k.k), sizeof(struct bch_inode_generation));
		return -BCH_ERR_invalid_bkey;
	}

	return 0;
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

	/* ick */
	inode_u->bi_flags |= str_hash << INODE_STR_HASH_OFFSET;
	get_random_bytes(&inode_u->bi_hash_seed,
			 sizeof(inode_u->bi_hash_seed));
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
	u32 snapshot;
	int ret = 0;

	/*
	 * We're never going to be deleting partial extents, no need to use an
	 * extent iterator:
	 */
	bch2_trans_iter_init(trans, &iter, id, POS(inum.inum, 0),
			     BTREE_ITER_INTENT|BTREE_ITER_NOT_EXTENTS);

	while (1) {
		bch2_trans_begin(trans);

		ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
		if (ret)
			goto err;

		bch2_btree_iter_set_snapshot(&iter, snapshot);

		k = bch2_btree_iter_peek_upto(&iter, POS(inum.inum, U64_MAX));
		ret = bkey_err(k);
		if (ret)
			goto err;

		if (!k.k)
			break;

		bkey_init(&delete.k);
		delete.k.p = iter.pos;

		ret = bch2_trans_update(trans, &iter, &delete, 0) ?:
		      bch2_trans_commit(trans, NULL, NULL,
					BTREE_INSERT_NOFAIL);
err:
		if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
			break;
	}

	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_inode_rm(struct bch_fs *c, subvol_inum inum)
{
	struct btree_trans trans;
	struct btree_iter iter = { NULL };
	struct bkey_i_inode_generation delete;
	struct bch_inode_unpacked inode_u;
	struct bkey_s_c k;
	u32 snapshot;
	int ret;

	bch2_trans_init(&trans, c, 0, 1024);

	/*
	 * If this was a directory, there shouldn't be any real dirents left -
	 * but there could be whiteouts (from hash collisions) that we should
	 * delete:
	 *
	 * XXX: the dirent could ideally would delete whiteouts when they're no
	 * longer needed
	 */
	ret   = bch2_inode_delete_keys(&trans, inum, BTREE_ID_extents) ?:
		bch2_inode_delete_keys(&trans, inum, BTREE_ID_xattrs) ?:
		bch2_inode_delete_keys(&trans, inum, BTREE_ID_dirents);
	if (ret)
		goto err;
retry:
	bch2_trans_begin(&trans);

	ret = bch2_subvolume_get_snapshot(&trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	bch2_trans_iter_init(&trans, &iter, BTREE_ID_inodes,
			     SPOS(0, inum.inum, snapshot),
			     BTREE_ITER_INTENT|BTREE_ITER_CACHED);
	k = bch2_btree_iter_peek_slot(&iter);

	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!bkey_is_inode(k.k)) {
		bch2_fs_inconsistent(trans.c,
				     "inode %llu:%u not found when deleting",
				     inum.inum, snapshot);
		ret = -EIO;
		goto err;
	}

	bch2_inode_unpack(k, &inode_u);

	/* Subvolume root? */
	BUG_ON(inode_u.bi_subvol);

	bkey_inode_generation_init(&delete.k_i);
	delete.k.p = iter.pos;
	delete.v.bi_generation = cpu_to_le32(inode_u.bi_generation + 1);

	ret   = bch2_trans_update(&trans, &iter, &delete.k_i, 0) ?:
		bch2_trans_commit(&trans, NULL, NULL,
				BTREE_INSERT_NOFAIL);
err:
	bch2_trans_iter_exit(&trans, &iter);
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_exit(&trans);
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
	return bch2_trans_do(c, NULL, NULL, 0,
		bch2_inode_find_by_inum_trans(&trans, inum, inode));
}

int bch2_inode_nlink_inc(struct bch_inode_unpacked *bi)
{
	if (bi->bi_flags & BCH_INODE_UNLINKED)
		bi->bi_flags &= ~BCH_INODE_UNLINKED;
	else {
		if (bi->bi_nlink == U32_MAX)
			return -EINVAL;

		bi->bi_nlink++;
	}

	return 0;
}

void bch2_inode_nlink_dec(struct btree_trans *trans, struct bch_inode_unpacked *bi)
{
	if (bi->bi_nlink && (bi->bi_flags & BCH_INODE_UNLINKED)) {
		bch2_trans_inconsistent(trans, "inode %llu unlinked but link count nonzero",
					bi->bi_inum);
		return;
	}

	if (bi->bi_flags & BCH_INODE_UNLINKED) {
		bch2_trans_inconsistent(trans, "inode %llu link count underflow", bi->bi_inum);
		return;
	}

	if (bi->bi_nlink)
		bi->bi_nlink--;
	else
		bi->bi_flags |= BCH_INODE_UNLINKED;
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
