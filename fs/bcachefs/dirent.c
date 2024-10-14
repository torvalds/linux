// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "bkey_methods.h"
#include "btree_update.h"
#include "extents.h"
#include "dirent.h"
#include "fs.h"
#include "keylist.h"
#include "str_hash.h"
#include "subvolume.h"

#include <linux/dcache.h>

static unsigned bch2_dirent_name_bytes(struct bkey_s_c_dirent d)
{
	if (bkey_val_bytes(d.k) < offsetof(struct bch_dirent, d_name))
		return 0;

	unsigned bkey_u64s = bkey_val_u64s(d.k);
	unsigned bkey_bytes = bkey_u64s * sizeof(u64);
	u64 last_u64 = ((u64*)d.v)[bkey_u64s - 1];
#if CPU_BIG_ENDIAN
	unsigned trailing_nuls = last_u64 ? __builtin_ctzll(last_u64) / 8 : 64 / 8;
#else
	unsigned trailing_nuls = last_u64 ? __builtin_clzll(last_u64) / 8 : 64 / 8;
#endif

	return bkey_bytes -
		offsetof(struct bch_dirent, d_name) -
		trailing_nuls;
}

struct qstr bch2_dirent_get_name(struct bkey_s_c_dirent d)
{
	return (struct qstr) QSTR_INIT(d.v->d_name, bch2_dirent_name_bytes(d));
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
	struct qstr name = bch2_dirent_get_name(d);

	return bch2_dirent_hash(info, &name);
}

static bool dirent_cmp_key(struct bkey_s_c _l, const void *_r)
{
	struct bkey_s_c_dirent l = bkey_s_c_to_dirent(_l);
	const struct qstr l_name = bch2_dirent_get_name(l);
	const struct qstr *r_name = _r;

	return !qstr_eq(l_name, *r_name);
}

static bool dirent_cmp_bkey(struct bkey_s_c _l, struct bkey_s_c _r)
{
	struct bkey_s_c_dirent l = bkey_s_c_to_dirent(_l);
	struct bkey_s_c_dirent r = bkey_s_c_to_dirent(_r);
	const struct qstr l_name = bch2_dirent_get_name(l);
	const struct qstr r_name = bch2_dirent_get_name(r);

	return !qstr_eq(l_name, r_name);
}

static bool dirent_is_visible(subvol_inum inum, struct bkey_s_c k)
{
	struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);

	if (d.v->d_type == DT_SUBVOL)
		return le32_to_cpu(d.v->d_parent_subvol) == inum.subvol;
	return true;
}

const struct bch_hash_desc bch2_dirent_hash_desc = {
	.btree_id	= BTREE_ID_dirents,
	.key_type	= KEY_TYPE_dirent,
	.hash_key	= dirent_hash_key,
	.hash_bkey	= dirent_hash_bkey,
	.cmp_key	= dirent_cmp_key,
	.cmp_bkey	= dirent_cmp_bkey,
	.is_visible	= dirent_is_visible,
};

int bch2_dirent_validate(struct bch_fs *c, struct bkey_s_c k,
			 enum bch_validate_flags flags)
{
	struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);
	struct qstr d_name = bch2_dirent_get_name(d);
	int ret = 0;

	bkey_fsck_err_on(!d_name.len,
			 c, dirent_empty_name,
			 "empty name");

	bkey_fsck_err_on(bkey_val_u64s(k.k) > dirent_val_u64s(d_name.len),
			 c, dirent_val_too_big,
			 "value too big (%zu > %u)",
			 bkey_val_u64s(k.k), dirent_val_u64s(d_name.len));

	/*
	 * Check new keys don't exceed the max length
	 * (older keys may be larger.)
	 */
	bkey_fsck_err_on((flags & BCH_VALIDATE_commit) && d_name.len > BCH_NAME_MAX,
			 c, dirent_name_too_long,
			 "dirent name too big (%u > %u)",
			 d_name.len, BCH_NAME_MAX);

	bkey_fsck_err_on(d_name.len != strnlen(d_name.name, d_name.len),
			 c, dirent_name_embedded_nul,
			 "dirent has stray data after name's NUL");

	bkey_fsck_err_on((d_name.len == 1 && !memcmp(d_name.name, ".", 1)) ||
			 (d_name.len == 2 && !memcmp(d_name.name, "..", 2)),
			 c, dirent_name_dot_or_dotdot,
			 "invalid name");

	bkey_fsck_err_on(memchr(d_name.name, '/', d_name.len),
			 c, dirent_name_has_slash,
			 "name with /");

	bkey_fsck_err_on(d.v->d_type != DT_SUBVOL &&
			 le64_to_cpu(d.v->d_inum) == d.k->p.inode,
			 c, dirent_to_itself,
			 "dirent points to own directory");
fsck_err:
	return ret;
}

void bch2_dirent_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);
	struct qstr d_name = bch2_dirent_get_name(d);

	prt_printf(out, "%.*s -> ", d_name.len, d_name.name);

	if (d.v->d_type != DT_SUBVOL)
		prt_printf(out, "%llu", le64_to_cpu(d.v->d_inum));
	else
		prt_printf(out, "%u -> %u",
			   le32_to_cpu(d.v->d_parent_subvol),
			   le32_to_cpu(d.v->d_child_subvol));

	prt_printf(out, " type %s", bch2_d_type_str(d.v->d_type));
}

static struct bkey_i_dirent *dirent_create_key(struct btree_trans *trans,
				subvol_inum dir, u8 type,
				const struct qstr *name, u64 dst)
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

	if (type != DT_SUBVOL) {
		dirent->v.d_inum = cpu_to_le64(dst);
	} else {
		dirent->v.d_parent_subvol = cpu_to_le32(dir.subvol);
		dirent->v.d_child_subvol = cpu_to_le32(dst);
	}

	dirent->v.d_type = type;

	memcpy(dirent->v.d_name, name->name, name->len);
	memset(dirent->v.d_name + name->len, 0,
	       bkey_val_bytes(&dirent->k) -
	       offsetof(struct bch_dirent, d_name) -
	       name->len);

	EBUG_ON(bch2_dirent_name_bytes(dirent_i_to_s_c(dirent)) != name->len);

	return dirent;
}

int bch2_dirent_create_snapshot(struct btree_trans *trans,
			u32 dir_subvol, u64 dir, u32 snapshot,
			const struct bch_hash_info *hash_info,
			u8 type, const struct qstr *name, u64 dst_inum,
			u64 *dir_offset,
			enum btree_iter_update_trigger_flags flags)
{
	subvol_inum dir_inum = { .subvol = dir_subvol, .inum = dir };
	struct bkey_i_dirent *dirent;
	int ret;

	dirent = dirent_create_key(trans, dir_inum, type, name, dst_inum);
	ret = PTR_ERR_OR_ZERO(dirent);
	if (ret)
		return ret;

	dirent->k.p.inode	= dir;
	dirent->k.p.snapshot	= snapshot;

	ret = bch2_hash_set_in_snapshot(trans, bch2_dirent_hash_desc, hash_info,
					dir_inum, snapshot, &dirent->k_i,
					flags|BTREE_UPDATE_internal_snapshot_node);
	*dir_offset = dirent->k.p.offset;

	return ret;
}

int bch2_dirent_create(struct btree_trans *trans, subvol_inum dir,
		       const struct bch_hash_info *hash_info,
		       u8 type, const struct qstr *name, u64 dst_inum,
		       u64 *dir_offset,
		       enum btree_iter_update_trigger_flags flags)
{
	struct bkey_i_dirent *dirent;
	int ret;

	dirent = dirent_create_key(trans, dir, type, name, dst_inum);
	ret = PTR_ERR_OR_ZERO(dirent);
	if (ret)
		return ret;

	ret = bch2_hash_set(trans, bch2_dirent_hash_desc, hash_info,
			    dir, &dirent->k_i, flags);
	*dir_offset = dirent->k.p.offset;

	return ret;
}

static void dirent_copy_target(struct bkey_i_dirent *dst,
			       struct bkey_s_c_dirent src)
{
	dst->v.d_inum = src.v->d_inum;
	dst->v.d_type = src.v->d_type;
}

int bch2_dirent_read_target(struct btree_trans *trans, subvol_inum dir,
			    struct bkey_s_c_dirent d, subvol_inum *target)
{
	struct bch_subvolume s;
	int ret = 0;

	if (d.v->d_type == DT_SUBVOL &&
	    le32_to_cpu(d.v->d_parent_subvol) != dir.subvol)
		return 1;

	if (likely(d.v->d_type != DT_SUBVOL)) {
		target->subvol	= dir.subvol;
		target->inum	= le64_to_cpu(d.v->d_inum);
	} else {
		target->subvol	= le32_to_cpu(d.v->d_child_subvol);

		ret = bch2_subvolume_get(trans, target->subvol, true, BTREE_ITER_cached, &s);

		target->inum	= le64_to_cpu(s.inode);
	}

	return ret;
}

int bch2_dirent_rename(struct btree_trans *trans,
		subvol_inum src_dir, struct bch_hash_info *src_hash,
		subvol_inum dst_dir, struct bch_hash_info *dst_hash,
		const struct qstr *src_name, subvol_inum *src_inum, u64 *src_offset,
		const struct qstr *dst_name, subvol_inum *dst_inum, u64 *dst_offset,
		enum bch_rename_mode mode)
{
	struct btree_iter src_iter = { NULL };
	struct btree_iter dst_iter = { NULL };
	struct bkey_s_c old_src, old_dst = bkey_s_c_null;
	struct bkey_i_dirent *new_src = NULL, *new_dst = NULL;
	struct bpos dst_pos =
		POS(dst_dir.inum, bch2_dirent_hash(dst_hash, dst_name));
	unsigned src_update_flags = 0;
	bool delete_src, delete_dst;
	int ret = 0;

	memset(src_inum, 0, sizeof(*src_inum));
	memset(dst_inum, 0, sizeof(*dst_inum));

	/* Lookup src: */
	old_src = bch2_hash_lookup(trans, &src_iter, bch2_dirent_hash_desc,
				   src_hash, src_dir, src_name,
				   BTREE_ITER_intent);
	ret = bkey_err(old_src);
	if (ret)
		goto out;

	ret = bch2_dirent_read_target(trans, src_dir,
			bkey_s_c_to_dirent(old_src), src_inum);
	if (ret)
		goto out;

	/* Lookup dst: */
	if (mode == BCH_RENAME) {
		/*
		 * Note that we're _not_ checking if the target already exists -
		 * we're relying on the VFS to do that check for us for
		 * correctness:
		 */
		ret = bch2_hash_hole(trans, &dst_iter, bch2_dirent_hash_desc,
				     dst_hash, dst_dir, dst_name);
		if (ret)
			goto out;
	} else {
		old_dst = bch2_hash_lookup(trans, &dst_iter, bch2_dirent_hash_desc,
					    dst_hash, dst_dir, dst_name,
					    BTREE_ITER_intent);
		ret = bkey_err(old_dst);
		if (ret)
			goto out;

		ret = bch2_dirent_read_target(trans, dst_dir,
				bkey_s_c_to_dirent(old_dst), dst_inum);
		if (ret)
			goto out;
	}

	if (mode != BCH_RENAME_EXCHANGE)
		*src_offset = dst_iter.pos.offset;

	/* Create new dst key: */
	new_dst = dirent_create_key(trans, dst_dir, 0, dst_name, 0);
	ret = PTR_ERR_OR_ZERO(new_dst);
	if (ret)
		goto out;

	dirent_copy_target(new_dst, bkey_s_c_to_dirent(old_src));
	new_dst->k.p = dst_iter.pos;

	/* Create new src key: */
	if (mode == BCH_RENAME_EXCHANGE) {
		new_src = dirent_create_key(trans, src_dir, 0, src_name, 0);
		ret = PTR_ERR_OR_ZERO(new_src);
		if (ret)
			goto out;

		dirent_copy_target(new_src, bkey_s_c_to_dirent(old_dst));
		new_src->k.p = src_iter.pos;
	} else {
		new_src = bch2_trans_kmalloc(trans, sizeof(struct bkey_i));
		ret = PTR_ERR_OR_ZERO(new_src);
		if (ret)
			goto out;

		bkey_init(&new_src->k);
		new_src->k.p = src_iter.pos;

		if (bkey_le(dst_pos, src_iter.pos) &&
		    bkey_lt(src_iter.pos, dst_iter.pos)) {
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
				new_src = new_dst;
				new_src->k.p = src_iter.pos;
				goto out_set_src;
			} else {
				/* If we're overwriting, we can't insert new_dst
				 * at a different slot because it has to
				 * overwrite old_dst - just make sure to use a
				 * whiteout when deleting src:
				 */
				new_src->k.type = KEY_TYPE_hash_whiteout;
			}
		} else {
			/* Check if we need a whiteout to delete src: */
			ret = bch2_hash_needs_whiteout(trans, bch2_dirent_hash_desc,
						       src_hash, &src_iter);
			if (ret < 0)
				goto out;

			if (ret)
				new_src->k.type = KEY_TYPE_hash_whiteout;
		}
	}

	if (new_dst->v.d_type == DT_SUBVOL)
		new_dst->v.d_parent_subvol = cpu_to_le32(dst_dir.subvol);

	if ((mode == BCH_RENAME_EXCHANGE) &&
	    new_src->v.d_type == DT_SUBVOL)
		new_src->v.d_parent_subvol = cpu_to_le32(src_dir.subvol);

	ret = bch2_trans_update(trans, &dst_iter, &new_dst->k_i, 0);
	if (ret)
		goto out;
out_set_src:
	/*
	 * If we're deleting a subvolume we need to really delete the dirent,
	 * not just emit a whiteout in the current snapshot - there can only be
	 * single dirent that points to a given subvolume.
	 *
	 * IOW, we don't maintain multiple versions in different snapshots of
	 * dirents that point to subvolumes - dirents that point to subvolumes
	 * are only visible in one particular subvolume so it's not necessary,
	 * and it would be particularly confusing for fsck to have to deal with.
	 */
	delete_src = bkey_s_c_to_dirent(old_src).v->d_type == DT_SUBVOL &&
		new_src->k.p.snapshot != old_src.k->p.snapshot;

	delete_dst = old_dst.k &&
		bkey_s_c_to_dirent(old_dst).v->d_type == DT_SUBVOL &&
		new_dst->k.p.snapshot != old_dst.k->p.snapshot;

	if (!delete_src || !bkey_deleted(&new_src->k)) {
		ret = bch2_trans_update(trans, &src_iter, &new_src->k_i, src_update_flags);
		if (ret)
			goto out;
	}

	if (delete_src) {
		bch2_btree_iter_set_snapshot(&src_iter, old_src.k->p.snapshot);
		ret =   bch2_btree_iter_traverse(&src_iter) ?:
			bch2_btree_delete_at(trans, &src_iter, BTREE_UPDATE_internal_snapshot_node);
		if (ret)
			goto out;
	}

	if (delete_dst) {
		bch2_btree_iter_set_snapshot(&dst_iter, old_dst.k->p.snapshot);
		ret =   bch2_btree_iter_traverse(&dst_iter) ?:
			bch2_btree_delete_at(trans, &dst_iter, BTREE_UPDATE_internal_snapshot_node);
		if (ret)
			goto out;
	}

	if (mode == BCH_RENAME_EXCHANGE)
		*src_offset = new_src->k.p.offset;
	*dst_offset = new_dst->k.p.offset;
out:
	bch2_trans_iter_exit(trans, &src_iter);
	bch2_trans_iter_exit(trans, &dst_iter);
	return ret;
}

int bch2_dirent_lookup_trans(struct btree_trans *trans,
			     struct btree_iter *iter,
			     subvol_inum dir,
			     const struct bch_hash_info *hash_info,
			     const struct qstr *name, subvol_inum *inum,
			     unsigned flags)
{
	struct bkey_s_c k = bch2_hash_lookup(trans, iter, bch2_dirent_hash_desc,
					     hash_info, dir, name, flags);
	int ret = bkey_err(k);
	if (ret)
		goto err;

	ret = bch2_dirent_read_target(trans, dir, bkey_s_c_to_dirent(k), inum);
	if (ret > 0)
		ret = -ENOENT;
err:
	if (ret)
		bch2_trans_iter_exit(trans, iter);
	return ret;
}

u64 bch2_dirent_lookup(struct bch_fs *c, subvol_inum dir,
		       const struct bch_hash_info *hash_info,
		       const struct qstr *name, subvol_inum *inum)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter = { NULL };

	int ret = lockrestart_do(trans,
		bch2_dirent_lookup_trans(trans, &iter, dir, hash_info, name, inum, 0));
	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	return ret;
}

int bch2_empty_dir_snapshot(struct btree_trans *trans, u64 dir, u32 subvol, u32 snapshot)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	for_each_btree_key_upto_norestart(trans, iter, BTREE_ID_dirents,
			   SPOS(dir, 0, snapshot),
			   POS(dir, U64_MAX), 0, k, ret)
		if (k.k->type == KEY_TYPE_dirent) {
			struct bkey_s_c_dirent d = bkey_s_c_to_dirent(k);
			if (d.v->d_type == DT_SUBVOL && le32_to_cpu(d.v->d_parent_subvol) != subvol)
				continue;
			ret = -BCH_ERR_ENOTEMPTY_dir_not_empty;
			break;
		}
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

int bch2_empty_dir_trans(struct btree_trans *trans, subvol_inum dir)
{
	u32 snapshot;

	return bch2_subvolume_get_snapshot(trans, dir.subvol, &snapshot) ?:
		bch2_empty_dir_snapshot(trans, dir.inum, dir.subvol, snapshot);
}

static int bch2_dir_emit(struct dir_context *ctx, struct bkey_s_c_dirent d, subvol_inum target)
{
	struct qstr name = bch2_dirent_get_name(d);
	/*
	 * Although not required by the kernel code, updating ctx->pos is needed
	 * for the bcachefs FUSE driver. Without this update, the FUSE
	 * implementation will be stuck in an infinite loop when reading
	 * directories (via the bcachefs_fuse_readdir callback).
	 * In kernel space, ctx->pos is updated by the VFS code.
	 */
	ctx->pos = d.k->p.offset;
	bool ret = dir_emit(ctx, name.name,
		      name.len,
		      target.inum,
		      vfs_d_type(d.v->d_type));
	if (ret)
		ctx->pos = d.k->p.offset + 1;
	return ret;
}

int bch2_readdir(struct bch_fs *c, subvol_inum inum, struct dir_context *ctx)
{
	struct bkey_buf sk;
	bch2_bkey_buf_init(&sk);

	int ret = bch2_trans_run(c,
		for_each_btree_key_in_subvolume_upto(trans, iter, BTREE_ID_dirents,
				   POS(inum.inum, ctx->pos),
				   POS(inum.inum, U64_MAX),
				   inum.subvol, 0, k, ({
			if (k.k->type != KEY_TYPE_dirent)
				continue;

			/* dir_emit() can fault and block: */
			bch2_bkey_buf_reassemble(&sk, c, k);
			struct bkey_s_c_dirent dirent = bkey_i_to_s_c_dirent(sk.k);

			subvol_inum target;
			int ret2 = bch2_dirent_read_target(trans, inum, dirent, &target);
			if (ret2 > 0)
				continue;

			ret2 ?: drop_locks_do(trans, bch2_dir_emit(ctx, dirent, target));
		})));

	bch2_bkey_buf_exit(&sk, c);

	return ret < 0 ? ret : 0;
}
