// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_cache.h"
#include "btree_update.h"
#include "dirent.h"
#include "fsck.h"
#include "str_hash.h"
#include "subvolume.h"

static int bch2_dirent_has_target(struct btree_trans *trans, struct bkey_s_c_dirent d)
{
	if (d.v->d_type == DT_SUBVOL) {
		struct bch_subvolume subvol;
		int ret = bch2_subvolume_get(trans, le32_to_cpu(d.v->d_child_subvol),
					     false, &subvol);
		if (ret && !bch2_err_matches(ret, ENOENT))
			return ret;
		return !ret;
	} else {
		struct btree_iter iter;
		struct bkey_s_c k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_inodes,
				SPOS(0, le64_to_cpu(d.v->d_inum), d.k->p.snapshot), 0);
		int ret = bkey_err(k);
		if (ret)
			return ret;

		ret = bkey_is_inode(k.k);
		bch2_trans_iter_exit(trans, &iter);
		return ret;
	}
}

static noinline int fsck_rename_dirent(struct btree_trans *trans,
				       struct snapshots_seen *s,
				       const struct bch_hash_desc desc,
				       struct bch_hash_info *hash_info,
				       struct bkey_s_c_dirent old)
{
	struct qstr old_name = bch2_dirent_get_name(old);
	struct bkey_i_dirent *new = bch2_trans_kmalloc(trans, bkey_bytes(old.k) + 32);
	int ret = PTR_ERR_OR_ZERO(new);
	if (ret)
		return ret;

	bkey_dirent_init(&new->k_i);
	dirent_copy_target(new, old);
	new->k.p = old.k->p;

	for (unsigned i = 0; i < 1000; i++) {
		unsigned len = sprintf(new->v.d_name, "%.*s.fsck_renamed-%u",
				       old_name.len, old_name.name, i);
		unsigned u64s = BKEY_U64s + dirent_val_u64s(len, 0);

		if (u64s > U8_MAX)
			return -EINVAL;

		new->k.u64s = u64s;

		ret = bch2_hash_set_in_snapshot(trans, bch2_dirent_hash_desc, hash_info,
						(subvol_inum) { 0, old.k->p.inode },
						old.k->p.snapshot, &new->k_i,
						BTREE_UPDATE_internal_snapshot_node);
		if (!bch2_err_matches(ret, EEXIST))
			break;
	}

	if (ret)
		return ret;

	return bch2_fsck_update_backpointers(trans, s, desc, hash_info, &new->k_i);
}

static noinline int hash_pick_winner(struct btree_trans *trans,
				     const struct bch_hash_desc desc,
				     struct bch_hash_info *hash_info,
				     struct bkey_s_c k1,
				     struct bkey_s_c k2)
{
	if (bkey_val_bytes(k1.k) == bkey_val_bytes(k2.k) &&
	    !memcmp(k1.v, k2.v, bkey_val_bytes(k1.k)))
		return 0;

	switch (desc.btree_id) {
	case BTREE_ID_dirents: {
		int ret = bch2_dirent_has_target(trans, bkey_s_c_to_dirent(k1));
		if (ret < 0)
			return ret;
		if (!ret)
			return 0;

		ret = bch2_dirent_has_target(trans, bkey_s_c_to_dirent(k2));
		if (ret < 0)
			return ret;
		if (!ret)
			return 1;
		return 2;
	}
	default:
		return 0;
	}
}

static int repair_inode_hash_info(struct btree_trans *trans,
				  struct bch_inode_unpacked *snapshot_root)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	for_each_btree_key_reverse_norestart(trans, iter, BTREE_ID_inodes,
					     SPOS(0, snapshot_root->bi_inum, snapshot_root->bi_snapshot - 1),
					     BTREE_ITER_all_snapshots, k, ret) {
		if (k.k->p.offset != snapshot_root->bi_inum)
			break;
		if (!bkey_is_inode(k.k))
			continue;

		struct bch_inode_unpacked inode;
		ret = bch2_inode_unpack(k, &inode);
		if (ret)
			break;

		if (fsck_err_on(inode.bi_hash_seed	!= snapshot_root->bi_hash_seed ||
				INODE_STR_HASH(&inode)	!= INODE_STR_HASH(snapshot_root),
				trans, inode_snapshot_mismatch,
				"inode hash info in different snapshots don't match")) {
			inode.bi_hash_seed = snapshot_root->bi_hash_seed;
			SET_INODE_STR_HASH(&inode, INODE_STR_HASH(snapshot_root));
			ret = __bch2_fsck_write_inode(trans, &inode) ?:
				bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc) ?:
				-BCH_ERR_transaction_restart_nested;
			break;
		}
	}
fsck_err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/*
 * All versions of the same inode in different snapshots must have the same hash
 * seed/type: verify that the hash info we're using matches the root
 */
static noinline int check_inode_hash_info_matches_root(struct btree_trans *trans, u64 inum,
						       struct bch_hash_info *hash_info)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	for_each_btree_key_reverse_norestart(trans, iter, BTREE_ID_inodes, SPOS(0, inum, U32_MAX),
					     BTREE_ITER_all_snapshots, k, ret) {
		if (k.k->p.offset != inum)
			break;
		if (bkey_is_inode(k.k))
			goto found;
	}
	bch_err(c, "%s(): inum %llu not found", __func__, inum);
	ret = -BCH_ERR_fsck_repair_unimplemented;
	goto err;
found:;
	struct bch_inode_unpacked inode;
	ret = bch2_inode_unpack(k, &inode);
	if (ret)
		goto err;

	struct bch_hash_info hash2 = bch2_hash_info_init(c, &inode);
	if (hash_info->type != hash2.type ||
	    memcmp(&hash_info->siphash_key, &hash2.siphash_key, sizeof(hash2.siphash_key))) {
		ret = repair_inode_hash_info(trans, &inode);
		if (!ret) {
			bch_err(c, "inode hash info mismatch with root, but mismatch not found\n"
				"%u %llx %llx\n"
				"%u %llx %llx",
				hash_info->type,
				hash_info->siphash_key.k0,
				hash_info->siphash_key.k1,
				hash2.type,
				hash2.siphash_key.k0,
				hash2.siphash_key.k1);
			ret = -BCH_ERR_fsck_repair_unimplemented;
		}
	}
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int __bch2_str_hash_check_key(struct btree_trans *trans,
			      struct snapshots_seen *s,
			      const struct bch_hash_desc *desc,
			      struct bch_hash_info *hash_info,
			      struct btree_iter *k_iter, struct bkey_s_c hash_k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter = {};
	struct printbuf buf = PRINTBUF;
	struct bkey_s_c k;
	int ret = 0;

	u64 hash = desc->hash_bkey(hash_info, hash_k);
	if (hash_k.k->p.offset < hash)
		goto bad_hash;

	for_each_btree_key_norestart(trans, iter, desc->btree_id,
				     SPOS(hash_k.k->p.inode, hash, hash_k.k->p.snapshot),
				     BTREE_ITER_slots, k, ret) {
		if (bkey_eq(k.k->p, hash_k.k->p))
			break;

		if (k.k->type == desc->key_type &&
		    !desc->cmp_bkey(k, hash_k))
			goto duplicate_entries;

		if (bkey_deleted(k.k)) {
			bch2_trans_iter_exit(trans, &iter);
			goto bad_hash;
		}
	}
out:
	bch2_trans_iter_exit(trans, &iter);
	printbuf_exit(&buf);
	return ret;
bad_hash:
	/*
	 * Before doing any repair, check hash_info itself:
	 */
	ret = check_inode_hash_info_matches_root(trans, hash_k.k->p.inode, hash_info);
	if (ret)
		goto out;

	if (fsck_err(trans, hash_table_key_wrong_offset,
		     "hash table key at wrong offset: btree %s inode %llu offset %llu, hashed to %llu\n%s",
		     bch2_btree_id_str(desc->btree_id), hash_k.k->p.inode, hash_k.k->p.offset, hash,
		     (printbuf_reset(&buf),
		      bch2_bkey_val_to_text(&buf, c, hash_k), buf.buf))) {
		struct bkey_i *new = bch2_bkey_make_mut_noupdate(trans, hash_k);
		if (IS_ERR(new))
			return PTR_ERR(new);

		k = bch2_hash_set_or_get_in_snapshot(trans, &iter, *desc, hash_info,
				       (subvol_inum) { 0, hash_k.k->p.inode },
				       hash_k.k->p.snapshot, new,
				       STR_HASH_must_create|
				       BTREE_ITER_with_updates|
				       BTREE_UPDATE_internal_snapshot_node);
		ret = bkey_err(k);
		if (ret)
			goto out;
		if (k.k)
			goto duplicate_entries;

		ret =   bch2_hash_delete_at(trans, *desc, hash_info, k_iter,
					    BTREE_UPDATE_internal_snapshot_node) ?:
			bch2_fsck_update_backpointers(trans, s, *desc, hash_info, new) ?:
			bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc) ?:
			-BCH_ERR_transaction_restart_nested;
		goto out;
	}
fsck_err:
	goto out;
duplicate_entries:
	ret = hash_pick_winner(trans, *desc, hash_info, hash_k, k);
	if (ret < 0)
		goto out;

	if (!fsck_err(trans, hash_table_key_duplicate,
		      "duplicate hash table keys%s:\n%s",
		      ret != 2 ? "" : ", both point to valid inodes",
		      (printbuf_reset(&buf),
		       bch2_bkey_val_to_text(&buf, c, hash_k),
		       prt_newline(&buf),
		       bch2_bkey_val_to_text(&buf, c, k),
		       buf.buf)))
		goto out;

	switch (ret) {
	case 0:
		ret = bch2_hash_delete_at(trans, *desc, hash_info, k_iter, 0);
		break;
	case 1:
		ret = bch2_hash_delete_at(trans, *desc, hash_info, &iter, 0);
		break;
	case 2:
		ret = fsck_rename_dirent(trans, s, *desc, hash_info, bkey_s_c_to_dirent(hash_k)) ?:
			bch2_hash_delete_at(trans, *desc, hash_info, k_iter, 0);
		goto out;
	}

	ret = bch2_trans_commit(trans, NULL, NULL, 0) ?:
		-BCH_ERR_transaction_restart_nested;
	goto out;
}
