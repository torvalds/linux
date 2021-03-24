// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_key_cache.h"
#include "bkey_methods.h"
#include "btree_update.h"
#include "error.h"
#include "extents.h"
#include "inode.h"
#include "str_hash.h"
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
static const u8 bits_table[8] = {
	1  * 8 - 1,
	2  * 8 - 2,
	3  * 8 - 3,
	4  * 8 - 4,
	6  * 8 - 5,
	8  * 8 - 6,
	10 * 8 - 7,
	13 * 8 - 8,
};

static int inode_encode_field(u8 *out, u8 *end, u64 hi, u64 lo)
{
	__be64 in[2] = { cpu_to_be64(hi), cpu_to_be64(lo), };
	unsigned shift, bytes, bits = likely(!hi)
		? fls64(lo)
		: fls64(hi) + 64;

	for (shift = 1; shift <= 8; shift++)
		if (bits < bits_table[shift - 1])
			goto got_shift;

	BUG();
got_shift:
	bytes = byte_table[shift - 1];

	BUG_ON(out + bytes > end);

	memcpy(out, (u8 *) in + 16 - bytes, bytes);
	*out |= (1 << 8) >> shift;

	return bytes;
}

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

static noinline void bch2_inode_pack_v1(struct bkey_inode_buf *packed,
					const struct bch_inode_unpacked *inode)
{
	struct bkey_i_inode *k = &packed->inode;
	u8 *out = k->v.fields;
	u8 *end = (void *) &packed[1];
	u8 *last_nonzero_field = out;
	unsigned nr_fields = 0, last_nonzero_fieldnr = 0;
	unsigned bytes;

#define x(_name, _bits)							\
	out += inode_encode_field(out, end, 0, inode->_name);		\
	nr_fields++;							\
									\
	if (inode->_name) {						\
		last_nonzero_field = out;				\
		last_nonzero_fieldnr = nr_fields;			\
	}

	BCH_INODE_FIELDS()
#undef  x

	out = last_nonzero_field;
	nr_fields = last_nonzero_fieldnr;

	bytes = out - (u8 *) &packed->inode.v;
	set_bkey_val_bytes(&packed->inode.k, bytes);
	memset_u64s_tail(&packed->inode.v, 0, bytes);

	SET_INODE_NR_FIELDS(&k->v, nr_fields);
}

static void bch2_inode_pack_v2(struct bkey_inode_buf *packed,
			       const struct bch_inode_unpacked *inode)
{
	struct bkey_i_inode *k = &packed->inode;
	u8 *out = k->v.fields;
	u8 *end = (void *) &packed[1];
	u8 *last_nonzero_field = out;
	unsigned nr_fields = 0, last_nonzero_fieldnr = 0;
	unsigned bytes;
	int ret;

#define x(_name, _bits)							\
	nr_fields++;							\
									\
	if (inode->_name) {						\
		ret = bch2_varint_encode(out, inode->_name);		\
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

	SET_INODE_NR_FIELDS(&k->v, nr_fields);
}

void bch2_inode_pack(struct bch_fs *c,
		     struct bkey_inode_buf *packed,
		     const struct bch_inode_unpacked *inode)
{
	bkey_inode_init(&packed->inode.k_i);
	packed->inode.k.p.offset	= inode->bi_inum;
	packed->inode.v.bi_hash_seed	= inode->bi_hash_seed;
	packed->inode.v.bi_flags	= cpu_to_le32(inode->bi_flags);
	packed->inode.v.bi_mode		= cpu_to_le16(inode->bi_mode);

	if (c->sb.features & (1ULL << BCH_FEATURE_new_varint)) {
		SET_INODE_NEW_VARINT(&packed->inode.v, true);
		bch2_inode_pack_v2(packed, inode);
	} else {
		bch2_inode_pack_v1(packed, inode);
	}

	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG)) {
		struct bch_inode_unpacked unpacked;

		int ret = bch2_inode_unpack(inode_i_to_s_c(&packed->inode),
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

static int bch2_inode_unpack_v2(struct bkey_s_c_inode inode,
				struct bch_inode_unpacked *unpacked)
{
	const u8 *in = inode.v->fields;
	const u8 *end = bkey_val_end(inode);
	unsigned fieldnr = 0;
	int ret;
	u64 v[2];

#define x(_name, _bits)							\
	if (fieldnr < INODE_NR_FIELDS(inode.v)) {			\
		ret = bch2_varint_decode(in, end, &v[0]);		\
		if (ret < 0)						\
			return ret;					\
		in += ret;						\
									\
		if (_bits > 64) {					\
			ret = bch2_varint_decode(in, end, &v[1]);	\
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

int bch2_inode_unpack(struct bkey_s_c_inode inode,
		      struct bch_inode_unpacked *unpacked)
{
	unpacked->bi_inum	= inode.k->p.offset;
	unpacked->bi_hash_seed	= inode.v->bi_hash_seed;
	unpacked->bi_flags	= le32_to_cpu(inode.v->bi_flags);
	unpacked->bi_mode	= le16_to_cpu(inode.v->bi_mode);

	if (INODE_NEW_VARINT(inode.v)) {
		return bch2_inode_unpack_v2(inode, unpacked);
	} else {
		return bch2_inode_unpack_v1(inode, unpacked);
	}

	return 0;
}

struct btree_iter *bch2_inode_peek(struct btree_trans *trans,
				   struct bch_inode_unpacked *inode,
				   u64 inum, unsigned flags)
{
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	iter = bch2_trans_get_iter(trans, BTREE_ID_inodes, POS(0, inum),
				   BTREE_ITER_CACHED|flags);
	k = bch2_btree_iter_peek_cached(iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	ret = k.k->type == KEY_TYPE_inode ? 0 : -EIO;
	if (ret)
		goto err;

	ret = bch2_inode_unpack(bkey_s_c_to_inode(k), inode);
	if (ret)
		goto err;

	return iter;
err:
	bch2_trans_iter_put(trans, iter);
	return ERR_PTR(ret);
}

int bch2_inode_write(struct btree_trans *trans,
		     struct btree_iter *iter,
		     struct bch_inode_unpacked *inode)
{
	struct bkey_inode_buf *inode_p;

	inode_p = bch2_trans_kmalloc(trans, sizeof(*inode_p));
	if (IS_ERR(inode_p))
		return PTR_ERR(inode_p);

	bch2_inode_pack(trans->c, inode_p, inode);
	inode_p->inode.k.p.snapshot = iter->snapshot;
	bch2_trans_update(trans, iter, &inode_p->inode.k_i, 0);
	return 0;
}

const char *bch2_inode_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
		struct bkey_s_c_inode inode = bkey_s_c_to_inode(k);
		struct bch_inode_unpacked unpacked;

	if (k.k->p.inode)
		return "nonzero k.p.inode";

	if (bkey_val_bytes(k.k) < sizeof(struct bch_inode))
		return "incorrect value size";

	if (k.k->p.offset < BLOCKDEV_INODE_MAX)
		return "fs inode in blockdev range";

	if (INODE_STR_HASH(inode.v) >= BCH_STR_HASH_NR)
		return "invalid str hash type";

	if (bch2_inode_unpack(inode, &unpacked))
		return "invalid variable length fields";

	if (unpacked.bi_data_checksum >= BCH_CSUM_OPT_NR + 1)
		return "invalid data checksum type";

	if (unpacked.bi_compression >= BCH_COMPRESSION_OPT_NR + 1)
		return "invalid data checksum type";

	if ((unpacked.bi_flags & BCH_INODE_UNLINKED) &&
	    unpacked.bi_nlink != 0)
		return "flagged as unlinked but bi_nlink != 0";

	return NULL;
}

void bch2_inode_to_text(struct printbuf *out, struct bch_fs *c,
		       struct bkey_s_c k)
{
	struct bkey_s_c_inode inode = bkey_s_c_to_inode(k);
	struct bch_inode_unpacked unpacked;

	if (bch2_inode_unpack(inode, &unpacked)) {
		pr_buf(out, "(unpack error)");
		return;
	}

	pr_buf(out, "mode: %o ", unpacked.bi_mode);

#define x(_name, _bits)						\
	pr_buf(out, #_name ": %llu ", (u64) unpacked._name);
	BCH_INODE_FIELDS()
#undef  x
}

const char *bch2_inode_generation_invalid(const struct bch_fs *c,
					  struct bkey_s_c k)
{
	if (k.k->p.inode)
		return "nonzero k.p.inode";

	if (bkey_val_bytes(k.k) != sizeof(struct bch_inode_generation))
		return "incorrect value size";

	return NULL;
}

void bch2_inode_generation_to_text(struct printbuf *out, struct bch_fs *c,
				   struct bkey_s_c k)
{
	struct bkey_s_c_inode_generation gen = bkey_s_c_to_inode_generation(k);

	pr_buf(out, "generation: %u", le32_to_cpu(gen.v->bi_generation));
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
		BUG();
	case KEY_TYPE_inode_generation:
		return le32_to_cpu(bkey_s_c_to_inode_generation(k).v->bi_generation);
	default:
		return 0;
	}
}

int bch2_inode_create(struct btree_trans *trans,
		      struct bch_inode_unpacked *inode_u)
{
	struct bch_fs *c = trans->c;
	struct bkey_inode_buf *inode_p;
	struct btree_iter *iter = NULL;
	struct bkey_s_c k;
	u64 min, max, start, *hint;
	int ret;

	u64 cpu = raw_smp_processor_id();
	unsigned bits = (c->opts.inodes_32bit
		? 31 : 63) - c->inode_shard_bits;

	min = (cpu << bits);
	max = (cpu << bits) | ~(ULLONG_MAX << bits);

	min = max_t(u64, min, BLOCKDEV_INODE_MAX);
	hint = c->unused_inode_hints + cpu;

	start = READ_ONCE(*hint);

	if (start >= max || start < min)
		start = min;

	inode_p = bch2_trans_kmalloc(trans, sizeof(*inode_p));
	if (IS_ERR(inode_p))
		return PTR_ERR(inode_p);
again:
	for_each_btree_key(trans, iter, BTREE_ID_inodes, POS(0, start),
			   BTREE_ITER_SLOTS|BTREE_ITER_INTENT, k, ret) {
		if (bkey_cmp(iter->pos, POS(0, max)) > 0)
			break;

		/*
		 * There's a potential cache coherency issue with the btree key
		 * cache code here - we're iterating over the btree, skipping
		 * that cache. We should never see an empty slot that isn't
		 * actually empty due to a pending update in the key cache
		 * because the update that creates the inode isn't done with a
		 * cached iterator, but - better safe than sorry, check the
		 * cache before using a slot:
		 */
		if (k.k->type != KEY_TYPE_inode &&
		    !bch2_btree_key_cache_find(c, BTREE_ID_inodes, iter->pos))
			goto found_slot;
	}

	bch2_trans_iter_put(trans, iter);

	if (ret)
		return ret;

	if (start != min) {
		/* Retry from start */
		start = min;
		goto again;
	}

	return -ENOSPC;
found_slot:
	*hint			= k.k->p.offset;
	inode_u->bi_inum	= k.k->p.offset;
	inode_u->bi_generation	= bkey_generation(k);

	ret = bch2_inode_write(trans, iter, inode_u);
	bch2_trans_iter_put(trans, iter);
	return ret;
}

int bch2_inode_rm(struct bch_fs *c, u64 inode_nr, bool cached)
{
	struct btree_trans trans;
	struct btree_iter *iter = NULL;
	struct bkey_i_inode_generation delete;
	struct bpos start = POS(inode_nr, 0);
	struct bpos end = POS(inode_nr + 1, 0);
	struct bch_inode_unpacked inode_u;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	/*
	 * If this was a directory, there shouldn't be any real dirents left -
	 * but there could be whiteouts (from hash collisions) that we should
	 * delete:
	 *
	 * XXX: the dirent could ideally would delete whiteouts when they're no
	 * longer needed
	 */
	ret   = bch2_btree_delete_range_trans(&trans, BTREE_ID_extents,
					      start, end, NULL) ?:
		bch2_btree_delete_range_trans(&trans, BTREE_ID_xattrs,
					      start, end, NULL) ?:
		bch2_btree_delete_range_trans(&trans, BTREE_ID_dirents,
					      start, end, NULL);
	if (ret)
		goto err;
retry:
	bch2_trans_begin(&trans);

	if (cached) {
		iter = bch2_trans_get_iter(&trans, BTREE_ID_inodes, POS(0, inode_nr),
					   BTREE_ITER_CACHED|BTREE_ITER_INTENT);
		k = bch2_btree_iter_peek_cached(iter);
	} else {
		iter = bch2_trans_get_iter(&trans, BTREE_ID_inodes, POS(0, inode_nr),
					   BTREE_ITER_SLOTS|BTREE_ITER_INTENT);
		k = bch2_btree_iter_peek_slot(iter);
	}

	ret = bkey_err(k);
	if (ret)
		goto err;

	if (k.k->type != KEY_TYPE_inode) {
		bch2_fs_inconsistent(trans.c,
				     "inode %llu not found when deleting",
				     inode_nr);
		ret = -EIO;
		goto err;
	}

	bch2_inode_unpack(bkey_s_c_to_inode(k), &inode_u);

	bkey_inode_generation_init(&delete.k_i);
	delete.k.p = iter->pos;
	delete.v.bi_generation = cpu_to_le32(inode_u.bi_generation + 1);

	bch2_trans_update(&trans, iter, &delete.k_i, 0);

	ret = bch2_trans_commit(&trans, NULL, NULL,
				BTREE_INSERT_NOFAIL);
err:
	bch2_trans_iter_put(&trans, iter);
	if (ret == -EINTR)
		goto retry;

	bch2_trans_exit(&trans);
	return ret;
}

int __bch2_inode_find_by_inum_trans(struct btree_trans *trans, u64 inode_nr,
				    struct bch_inode_unpacked *inode,
				    unsigned flags)
{
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	iter = bch2_trans_get_iter(trans, BTREE_ID_inodes,
			POS(0, inode_nr), flags);
	k = (flags & BTREE_ITER_TYPE) == BTREE_ITER_CACHED
		? bch2_btree_iter_peek_cached(iter)
		: bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	ret = k.k->type == KEY_TYPE_inode
		? bch2_inode_unpack(bkey_s_c_to_inode(k), inode)
		: -ENOENT;
err:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

int bch2_inode_find_by_inum_trans(struct btree_trans *trans, u64 inode_nr,
				  struct bch_inode_unpacked *inode)
{
	return __bch2_inode_find_by_inum_trans(trans, inode_nr,
					       inode, BTREE_ITER_CACHED);

}

int bch2_inode_find_by_inum(struct bch_fs *c, u64 inode_nr,
			    struct bch_inode_unpacked *inode)
{
	return bch2_trans_do(c, NULL, NULL, 0,
		bch2_inode_find_by_inum_trans(&trans, inode_nr, inode));
}
