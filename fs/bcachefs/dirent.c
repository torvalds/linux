// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_methods.h"
#include "btree_update.h"
#include "extents.h"
#include "dirent.h"
#include "fs.h"
#include "keylist.h"
#include "str_hash.h"

#include <linux/dcache.h>

unsigned bch2_dirent_name_bytes(struct bkey_s_c_dirent d)
{
	unsigned len = bkey_val_bytes(d.k) -
		offsetof(struct bch_dirent, d_name);

	return strnlen(d.v->d_name, len);
}

static u64 bch2_dirent_hash(const struct bch_hash_info *info,
			    const struct qstr *name)
{
	struct bch_str_hash_ctx ctx;

	bch2_str_hash_init(&ctx, info);
	bch2_str_hash_update(&ctx, info, name->name, name->len);

	/* [0,2) reserved for dots */
	return max_t(u64, bch2_str_hash_end(&ctx, info), 2);
}

static u64 dirent_hash_key(const struct bch_hash_info *info, const void *key)
{
	return bch2_dirent_hash(info, key);
}

static u64 dirent_hash_bkey(const struct bch_hash_info *info, struct bkey_s_c k)
{
	struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);
	struct qstr name = QSTR_INIT(d.v->d_name, bch2_dirent_name_bytes(d));

	return bch2_dirent_hash(info, &name);
}

static bool dirent_cmp_key(struct bkey_s_c _l, const void *_r)
{
	struct bkey_s_c_dirent l = bkey_s_c_to_dirent(_l);
	int len = bch2_dirent_name_bytes(l);
	const struct qstr *r = _r;

	return len - r->len ?: memcmp(l.v->d_name, r->name, len);
}

static bool dirent_cmp_bkey(struct bkey_s_c _l, struct bkey_s_c _r)
{
	struct bkey_s_c_dirent l = bkey_s_c_to_dirent(_l);
	struct bkey_s_c_dirent r = bkey_s_c_to_dirent(_r);
	int l_len = bch2_dirent_name_bytes(l);
	int r_len = bch2_dirent_name_bytes(r);

	return l_len - r_len ?: memcmp(l.v->d_name, r.v->d_name, l_len);
}

const struct bch_hash_desc bch2_dirent_hash_desc = {
	.btree_id	= BTREE_ID_DIRENTS,
	.key_type	= KEY_TYPE_dirent,
	.hash_key	= dirent_hash_key,
	.hash_bkey	= dirent_hash_bkey,
	.cmp_key	= dirent_cmp_key,
	.cmp_bkey	= dirent_cmp_bkey,
};

const char *bch2_dirent_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);
	unsigned len;

	if (bkey_val_bytes(k.k) < sizeof(struct bch_dirent))
		return "value too small";

	len = bch2_dirent_name_bytes(d);
	if (!len)
		return "empty name";

	/*
	 * older versions of bcachefs were buggy and creating dirent
	 * keys that were bigger than necessary:
	 */
	if (bkey_val_u64s(k.k) > dirent_val_u64s(len + 7))
		return "value too big";

	if (len > BCH_NAME_MAX)
		return "dirent name too big";

	return NULL;
}

void bch2_dirent_to_text(struct printbuf *out, struct bch_fs *c,
			 struct bkey_s_c k)
{
	struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);

	bch_scnmemcpy(out, d.v->d_name,
		      bch2_dirent_name_bytes(d));
	pr_buf(out, " -> %llu", d.v->d_inum);
}

static struct bkey_i_dirent *dirent_create_key(struct btree_trans *trans,
				u8 type, const struct qstr *name, u64 dst)
{
	struct bkey_i_dirent *dirent;
	unsigned u64s = BKEY_U64s + dirent_val_u64s(name->len);

	if (name->len > BCH_NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	BUG_ON(u64s > U8_MAX);

	dirent = bch2_trans_kmalloc(trans, u64s * sizeof(u64));
	if (IS_ERR(dirent))
		return dirent;

	bkey_dirent_init(&dirent->k_i);
	dirent->k.u64s = u64s;
	dirent->v.d_inum = cpu_to_le64(dst);
	dirent->v.d_type = type;

	memcpy(dirent->v.d_name, name->name, name->len);
	memset(dirent->v.d_name + name->len, 0,
	       bkey_val_bytes(&dirent->k) -
	       offsetof(struct bch_dirent, d_name) -
	       name->len);

	EBUG_ON(bch2_dirent_name_bytes(dirent_i_to_s_c(dirent)) != name->len);

	return dirent;
}

int __bch2_dirent_create(struct btree_trans *trans,
			 u64 dir_inum, const struct bch_hash_info *hash_info,
			 u8 type, const struct qstr *name, u64 dst_inum,
			 int flags)
{
	struct bkey_i_dirent *dirent;
	int ret;

	dirent = dirent_create_key(trans, type, name, dst_inum);
	ret = PTR_ERR_OR_ZERO(dirent);
	if (ret)
		return ret;

	return bch2_hash_set(trans, bch2_dirent_hash_desc, hash_info,
			     dir_inum, &dirent->k_i, flags);
}

int bch2_dirent_create(struct bch_fs *c, u64 dir_inum,
		       const struct bch_hash_info *hash_info,
		       u8 type, const struct qstr *name, u64 dst_inum,
		       u64 *journal_seq, int flags)
{
	return bch2_trans_do(c, journal_seq, flags,
		__bch2_dirent_create(&trans, dir_inum, hash_info,
				     type, name, dst_inum, flags));
}

static void dirent_copy_target(struct bkey_i_dirent *dst,
			       struct bkey_s_c_dirent src)
{
	dst->v.d_inum = src.v->d_inum;
	dst->v.d_type = src.v->d_type;
}

static struct bpos bch2_dirent_pos(struct bch_inode_info *inode,
				   const struct qstr *name)
{
	return POS(inode->v.i_ino, bch2_dirent_hash(&inode->ei_str_hash, name));
}

int bch2_dirent_rename(struct btree_trans *trans,
		struct bch_inode_info *src_dir, const struct qstr *src_name,
		struct bch_inode_info *dst_dir, const struct qstr *dst_name,
		enum bch_rename_mode mode)
{
	struct btree_iter *src_iter, *dst_iter;
	struct bkey_s_c old_src, old_dst;
	struct bkey_i_dirent *new_src = NULL, *new_dst = NULL;
	struct bpos dst_pos = bch2_dirent_pos(dst_dir, dst_name);
	int ret;

	/*
	 * Lookup dst:
	 *
	 * Note that in BCH_RENAME mode, we're _not_ checking if
	 * the target already exists - we're relying on the VFS
	 * to do that check for us for correctness:
	 */
	dst_iter = mode == BCH_RENAME
		? bch2_hash_hole(trans, bch2_dirent_hash_desc,
				 &dst_dir->ei_str_hash,
				 dst_dir->v.i_ino, dst_name)
		: bch2_hash_lookup(trans, bch2_dirent_hash_desc,
				   &dst_dir->ei_str_hash,
				   dst_dir->v.i_ino, dst_name,
				   BTREE_ITER_INTENT);
	if (IS_ERR(dst_iter))
		return PTR_ERR(dst_iter);
	old_dst = bch2_btree_iter_peek_slot(dst_iter);

	/* Lookup src: */
	src_iter = bch2_hash_lookup(trans, bch2_dirent_hash_desc,
				    &src_dir->ei_str_hash,
				    src_dir->v.i_ino, src_name,
				    BTREE_ITER_INTENT);
	if (IS_ERR(src_iter))
		return PTR_ERR(src_iter);
	old_src = bch2_btree_iter_peek_slot(src_iter);

	/* Create new dst key: */
	new_dst = dirent_create_key(trans, 0, dst_name, 0);
	if (IS_ERR(new_dst))
		return PTR_ERR(new_dst);

	dirent_copy_target(new_dst, bkey_s_c_to_dirent(old_src));
	new_dst->k.p = dst_iter->pos;

	/* Create new src key: */
	if (mode == BCH_RENAME_EXCHANGE) {
		new_src = dirent_create_key(trans, 0, src_name, 0);
		if (IS_ERR(new_src))
			return PTR_ERR(new_src);

		dirent_copy_target(new_src, bkey_s_c_to_dirent(old_dst));
		new_src->k.p = src_iter->pos;
	} else {
		new_src = bch2_trans_kmalloc(trans, sizeof(struct bkey_i));
		if (IS_ERR(new_src))
			return PTR_ERR(new_src);
		bkey_init(&new_src->k);
		new_src->k.p = src_iter->pos;

		if (bkey_cmp(dst_pos, src_iter->pos) <= 0 &&
		    bkey_cmp(src_iter->pos, dst_iter->pos) < 0) {
			/*
			 * We have a hash collision for the new dst key,
			 * and new_src - the key we're deleting - is between
			 * new_dst's hashed slot and the slot we're going to be
			 * inserting it into - oops.  This will break the hash
			 * table if we don't deal with it:
			 */
			if (mode == BCH_RENAME) {
				/*
				 * If we're not overwriting, we can just insert
				 * new_dst at the src position:
				 */
				new_dst->k.p = src_iter->pos;
				bch2_trans_update(trans, src_iter,
						  &new_dst->k_i);
				return 0;
			} else {
				/* If we're overwriting, we can't insert new_dst
				 * at a different slot because it has to
				 * overwrite old_dst - just make sure to use a
				 * whiteout when deleting src:
				 */
				new_src->k.type = KEY_TYPE_whiteout;
			}
		} else {
			/* Check if we need a whiteout to delete src: */
			ret = bch2_hash_needs_whiteout(trans, bch2_dirent_hash_desc,
						       &src_dir->ei_str_hash,
						       src_iter);
			if (ret < 0)
				return ret;

			if (ret)
				new_src->k.type = KEY_TYPE_whiteout;
		}
	}

	bch2_trans_update(trans, src_iter, &new_src->k_i);
	bch2_trans_update(trans, dst_iter, &new_dst->k_i);
	return 0;
}

int __bch2_dirent_delete(struct btree_trans *trans, u64 dir_inum,
			 const struct bch_hash_info *hash_info,
			 const struct qstr *name)
{
	return bch2_hash_delete(trans, bch2_dirent_hash_desc, hash_info,
				dir_inum, name);
}

int bch2_dirent_delete(struct bch_fs *c, u64 dir_inum,
		       const struct bch_hash_info *hash_info,
		       const struct qstr *name,
		       u64 *journal_seq)
{
	return bch2_trans_do(c, journal_seq,
			     BTREE_INSERT_ATOMIC|
			     BTREE_INSERT_NOFAIL,
		__bch2_dirent_delete(&trans, dir_inum, hash_info, name));
}

u64 bch2_dirent_lookup(struct bch_fs *c, u64 dir_inum,
		       const struct bch_hash_info *hash_info,
		       const struct qstr *name)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 inum = 0;

	bch2_trans_init(&trans, c, 0, 0);

	iter = bch2_hash_lookup(&trans, bch2_dirent_hash_desc,
				hash_info, dir_inum, name, 0);
	if (IS_ERR(iter)) {
		BUG_ON(PTR_ERR(iter) == -EINTR);
		goto out;
	}

	k = bch2_btree_iter_peek_slot(iter);
	inum = le64_to_cpu(bkey_s_c_to_dirent(k).v->d_inum);
out:
	bch2_trans_exit(&trans);
	return inum;
}

int bch2_empty_dir_trans(struct btree_trans *trans, u64 dir_inum)
{
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	for_each_btree_key(trans, iter, BTREE_ID_DIRENTS,
			   POS(dir_inum, 0), 0, k, ret) {
		if (k.k->p.inode > dir_inum)
			break;

		if (k.k->type == KEY_TYPE_dirent) {
			ret = -ENOTEMPTY;
			break;
		}
	}
	bch2_trans_iter_put(trans, iter);

	return ret;
}

int bch2_empty_dir(struct bch_fs *c, u64 dir_inum)
{
	return bch2_trans_do(c, NULL, 0,
		bch2_empty_dir_trans(&trans, dir_inum));
}

int bch2_readdir(struct bch_fs *c, struct file *file,
		 struct dir_context *ctx)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_s_c_dirent dirent;
	unsigned len;
	int ret;

	if (!dir_emit_dots(file, ctx))
		return 0;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_DIRENTS,
			   POS(inode->v.i_ino, ctx->pos), 0, k, ret) {
		if (k.k->type != KEY_TYPE_dirent)
			continue;

		dirent = bkey_s_c_to_dirent(k);

		if (bkey_cmp(k.k->p, POS(inode->v.i_ino, ctx->pos)) < 0)
			continue;

		if (k.k->p.inode > inode->v.i_ino)
			break;

		len = bch2_dirent_name_bytes(dirent);

		/*
		 * XXX: dir_emit() can fault and block, while we're holding
		 * locks
		 */
		if (!dir_emit(ctx, dirent.v->d_name, len,
			      le64_to_cpu(dirent.v->d_inum),
			      dirent.v->d_type))
			break;

		ctx->pos = k.k->p.offset + 1;
	}
	ret = bch2_trans_exit(&trans) ?: ret;

	return ret;
}
