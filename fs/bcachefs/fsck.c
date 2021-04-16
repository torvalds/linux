// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "dirent.h"
#include "error.h"
#include "fs-common.h"
#include "fsck.h"
#include "inode.h"
#include "keylist.h"
#include "super.h"
#include "xattr.h"

#include <linux/dcache.h> /* struct qstr */
#include <linux/generic-radix-tree.h>

#define QSTR(n) { { { .len = strlen(n) } }, .name = n }

static s64 bch2_count_inode_sectors(struct btree_trans *trans, u64 inum)
{
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 sectors = 0;
	int ret;

	for_each_btree_key(trans, iter, BTREE_ID_extents,
			   POS(inum, 0), 0, k, ret) {
		if (k.k->p.inode != inum)
			break;

		if (bkey_extent_is_allocation(k.k))
			sectors += k.k->size;
	}

	bch2_trans_iter_free(trans, iter);

	return ret ?: sectors;
}

static int lookup_inode(struct btree_trans *trans, u64 inode_nr,
			struct bch_inode_unpacked *inode,
			u32 *snapshot)
{
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	iter = bch2_trans_get_iter(trans, BTREE_ID_inodes,
			POS(0, inode_nr), 0);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (snapshot)
		*snapshot = iter->pos.snapshot;
	ret = k.k->type == KEY_TYPE_inode
		? bch2_inode_unpack(bkey_s_c_to_inode(k), inode)
		: -ENOENT;
err:
	bch2_trans_iter_free(trans, iter);
	return ret;
}

static int write_inode(struct btree_trans *trans,
		       struct bch_inode_unpacked *inode,
		       u32 snapshot)
{
	struct btree_iter *inode_iter =
		bch2_trans_get_iter(trans, BTREE_ID_inodes,
				    SPOS(0, inode->bi_inum, snapshot),
				    BTREE_ITER_INTENT);
	int ret = __bch2_trans_do(trans, NULL, NULL,
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW,
				  bch2_inode_write(trans, inode_iter, inode));
	bch2_trans_iter_put(trans, inode_iter);
	if (ret)
		bch_err(trans->c, "error in fsck: error %i updating inode", ret);
	return ret;
}

static int __remove_dirent(struct btree_trans *trans, struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter;
	struct bch_inode_unpacked dir_inode;
	struct bch_hash_info dir_hash_info;
	int ret;

	ret = lookup_inode(trans, pos.inode, &dir_inode, NULL);
	if (ret)
		return ret;

	dir_hash_info = bch2_hash_info_init(c, &dir_inode);

	iter = bch2_trans_get_iter(trans, BTREE_ID_dirents, pos, BTREE_ITER_INTENT);

	ret = bch2_hash_delete_at(trans, bch2_dirent_hash_desc,
				  &dir_hash_info, iter);
	bch2_trans_iter_put(trans, iter);
	return ret;
}

static int remove_dirent(struct btree_trans *trans, struct bpos pos)
{
	int ret = __bch2_trans_do(trans, NULL, NULL,
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW,
				  __remove_dirent(trans, pos));
	if (ret)
		bch_err(trans->c, "remove_dirent: err %i deleting dirent", ret);
	return ret;
}

static int __reattach_inode(struct btree_trans *trans,
			    struct bch_inode_unpacked *lostfound,
			    u64 inum)
{
	struct bch_hash_info dir_hash =
		bch2_hash_info_init(trans->c, lostfound);
	struct bch_inode_unpacked inode_u;
	char name_buf[20];
	struct qstr name;
	u64 dir_offset = 0;
	u32 snapshot;
	int ret;

	snprintf(name_buf, sizeof(name_buf), "%llu", inum);
	name = (struct qstr) QSTR(name_buf);

	ret = lookup_inode(trans, inum, &inode_u, &snapshot);
	if (ret)
		return ret;

	if (S_ISDIR(inode_u.bi_mode)) {
		lostfound->bi_nlink++;

		ret = write_inode(trans, lostfound, U32_MAX);
		if (ret)
			return ret;
	}

	ret = bch2_dirent_create(trans, lostfound->bi_inum, &dir_hash,
				 mode_to_type(inode_u.bi_mode),
				 &name, inum, &dir_offset,
				 BCH_HASH_SET_MUST_CREATE);
	if (ret)
		return ret;

	inode_u.bi_dir		= lostfound->bi_inum;
	inode_u.bi_dir_offset	= dir_offset;

	return write_inode(trans, &inode_u, U32_MAX);
}

static int reattach_inode(struct btree_trans *trans,
			  struct bch_inode_unpacked *lostfound,
			  u64 inum)
{
	int ret = __bch2_trans_do(trans, NULL, NULL, BTREE_INSERT_LAZY_RW,
			      __reattach_inode(trans, lostfound, inum));
	if (ret)
		bch_err(trans->c, "error %i reattaching inode %llu", ret, inum);

	return ret;
}

static int remove_backpointer(struct btree_trans *trans,
			      struct bch_inode_unpacked *inode)
{
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	iter = bch2_trans_get_iter(trans, BTREE_ID_dirents,
				   POS(inode->bi_dir, inode->bi_dir_offset), 0);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		goto out;
	if (k.k->type != KEY_TYPE_dirent) {
		ret = -ENOENT;
		goto out;
	}

	ret = remove_dirent(trans, k.k->p);
out:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

struct inode_walker {
	bool			first_this_inode;
	bool			have_inode;
	u64			cur_inum;
	u32			snapshot;
	struct bch_inode_unpacked inode;
};

static struct inode_walker inode_walker_init(void)
{
	return (struct inode_walker) {
		.cur_inum	= -1,
		.have_inode	= false,
	};
}

static int walk_inode(struct btree_trans *trans,
		      struct inode_walker *w, u64 inum)
{
	if (inum != w->cur_inum) {
		int ret = lookup_inode(trans, inum, &w->inode, &w->snapshot);

		if (ret && ret != -ENOENT)
			return ret;

		w->have_inode	= !ret;
		w->cur_inum	= inum;
		w->first_this_inode = true;
	} else {
		w->first_this_inode = false;
	}

	return 0;
}

static int hash_redo_key(struct btree_trans *trans,
			 const struct bch_hash_desc desc,
			 struct bch_hash_info *hash_info,
			 struct btree_iter *k_iter, struct bkey_s_c k)
{
	struct bkey_i delete;
	struct bkey_i *tmp;

	tmp = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	bkey_reassemble(tmp, k);

	bkey_init(&delete.k);
	delete.k.p = k_iter->pos;
	bch2_trans_update(trans, k_iter, &delete, 0);

	return bch2_hash_set(trans, desc, hash_info, k_iter->pos.inode,
			     tmp, 0);
}

static int fsck_hash_delete_at(struct btree_trans *trans,
			       const struct bch_hash_desc desc,
			       struct bch_hash_info *info,
			       struct btree_iter *iter)
{
	int ret;
retry:
	ret   = bch2_hash_delete_at(trans, desc, info, iter) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW);
	if (ret == -EINTR) {
		ret = bch2_btree_iter_traverse(iter);
		if (!ret)
			goto retry;
	}

	return ret;
}

static int hash_check_key(struct btree_trans *trans,
			  const struct bch_hash_desc desc,
			  struct bch_hash_info *hash_info,
			  struct btree_iter *k_iter, struct bkey_s_c hash_k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter = NULL;
	char buf[200];
	struct bkey_s_c k;
	u64 hash;
	int ret = 0;

	if (hash_k.k->type != desc.key_type)
		return 0;

	hash = desc.hash_bkey(hash_info, hash_k);

	if (likely(hash == hash_k.k->p.offset))
		return 0;

	if (hash_k.k->p.offset < hash)
		goto bad_hash;

	for_each_btree_key(trans, iter, desc.btree_id, POS(hash_k.k->p.inode, hash),
			   BTREE_ITER_SLOTS, k, ret) {
		if (!bkey_cmp(k.k->p, hash_k.k->p))
			break;

		if (fsck_err_on(k.k->type == desc.key_type &&
				!desc.cmp_bkey(k, hash_k), c,
				"duplicate hash table keys:\n%s",
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       hash_k), buf))) {
			ret = fsck_hash_delete_at(trans, desc, hash_info, k_iter);
			if (ret)
				return ret;
			ret = 1;
			break;
		}

		if (bkey_deleted(k.k)) {
			bch2_trans_iter_free(trans, iter);
			goto bad_hash;
		}

	}
	bch2_trans_iter_free(trans, iter);
	return ret;
bad_hash:
	if (fsck_err(c, "hash table key at wrong offset: btree %u inode %llu offset %llu, "
		     "hashed to %llu should be at %llu\n%s",
		     desc.btree_id, hash_k.k->p.inode, hash_k.k->p.offset,
		     hash, iter->pos.offset,
		     (bch2_bkey_val_to_text(&PBUF(buf), c, hash_k), buf)) == FSCK_ERR_IGNORE)
		return 0;

	ret = __bch2_trans_do(trans, NULL, NULL,
			      BTREE_INSERT_NOFAIL|BTREE_INSERT_LAZY_RW,
		hash_redo_key(trans, desc, hash_info, k_iter, hash_k));
	if (ret) {
		bch_err(c, "hash_redo_key err %i", ret);
		return ret;
	}
	return -EINTR;
fsck_err:
	return ret;
}

static int check_inode(struct btree_trans *trans,
		       struct btree_iter *iter,
		       struct bkey_s_c_inode inode)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked u;
	bool do_update = false;
	int ret = 0;

	ret = bch2_inode_unpack(inode, &u);

	if (bch2_fs_inconsistent_on(ret, c,
			 "error unpacking inode %llu in fsck",
			 inode.k->p.inode))
		return ret;

	if (u.bi_flags & BCH_INODE_UNLINKED &&
	    (!c->sb.clean ||
	     fsck_err(c, "filesystem marked clean, but inode %llu unlinked",
		      u.bi_inum))) {
		bch_verbose(c, "deleting inode %llu", u.bi_inum);

		bch2_trans_unlock(trans);
		bch2_fs_lazy_rw(c);

		ret = bch2_inode_rm(c, u.bi_inum, false);
		if (ret)
			bch_err(c, "error in fsck: error %i while deleting inode", ret);
		return ret;
	}

	if (u.bi_flags & BCH_INODE_I_SIZE_DIRTY &&
	    (!c->sb.clean ||
	     fsck_err(c, "filesystem marked clean, but inode %llu has i_size dirty",
		      u.bi_inum))) {
		bch_verbose(c, "truncating inode %llu", u.bi_inum);

		bch2_trans_unlock(trans);
		bch2_fs_lazy_rw(c);

		/*
		 * XXX: need to truncate partial blocks too here - or ideally
		 * just switch units to bytes and that issue goes away
		 */
		ret = bch2_btree_delete_range_trans(trans, BTREE_ID_extents,
				POS(u.bi_inum, round_up(u.bi_size, block_bytes(c)) >> 9),
				POS(u.bi_inum, U64_MAX),
				NULL);
		if (ret) {
			bch_err(c, "error in fsck: error %i truncating inode", ret);
			return ret;
		}

		/*
		 * We truncated without our normal sector accounting hook, just
		 * make sure we recalculate it:
		 */
		u.bi_flags |= BCH_INODE_I_SECTORS_DIRTY;

		u.bi_flags &= ~BCH_INODE_I_SIZE_DIRTY;
		do_update = true;
	}

	if (u.bi_flags & BCH_INODE_I_SECTORS_DIRTY &&
	    (!c->sb.clean ||
	     fsck_err(c, "filesystem marked clean, but inode %llu has i_sectors dirty",
		      u.bi_inum))) {
		s64 sectors;

		bch_verbose(c, "recounting sectors for inode %llu",
			    u.bi_inum);

		sectors = bch2_count_inode_sectors(trans, u.bi_inum);
		if (sectors < 0) {
			bch_err(c, "error in fsck: error %i recounting inode sectors",
				(int) sectors);
			return sectors;
		}

		u.bi_sectors = sectors;
		u.bi_flags &= ~BCH_INODE_I_SECTORS_DIRTY;
		do_update = true;
	}

	if (u.bi_flags & BCH_INODE_BACKPTR_UNTRUSTED) {
		u.bi_dir = 0;
		u.bi_dir_offset = 0;
		u.bi_flags &= ~BCH_INODE_BACKPTR_UNTRUSTED;
		do_update = true;
	}

	if (do_update) {
		ret = __bch2_trans_do(trans, NULL, NULL,
				      BTREE_INSERT_NOFAIL|
				      BTREE_INSERT_LAZY_RW,
				bch2_inode_write(trans, iter, &u));
		if (ret)
			bch_err(c, "error in fsck: error %i "
				"updating inode", ret);
	}
fsck_err:
	return ret;
}

noinline_for_stack
static int check_inodes(struct bch_fs *c, bool full)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_s_c_inode inode;
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_inodes, POS_MIN, 0, k, ret) {
		if (k.k->type != KEY_TYPE_inode)
			continue;

		inode = bkey_s_c_to_inode(k);

		if (full ||
		    (inode.v->bi_flags & (BCH_INODE_I_SIZE_DIRTY|
					  BCH_INODE_I_SECTORS_DIRTY|
					  BCH_INODE_UNLINKED))) {
			ret = check_inode(&trans, iter, inode);
			if (ret)
				break;
		}
	}
	bch2_trans_iter_put(&trans, iter);

	BUG_ON(ret == -EINTR);

	return bch2_trans_exit(&trans) ?: ret;
}

static int fix_overlapping_extent(struct btree_trans *trans,
				       struct bkey_s_c k, struct bpos cut_at)
{
	struct btree_iter *iter;
	struct bkey_i *u;
	int ret;

	u = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
	ret = PTR_ERR_OR_ZERO(u);
	if (ret)
		return ret;

	bkey_reassemble(u, k);
	bch2_cut_front(cut_at, u);


	/*
	 * We don't want to go through the extent_handle_overwrites path:
	 *
	 * XXX: this is going to screw up disk accounting, extent triggers
	 * assume things about extent overwrites - we should be running the
	 * triggers manually here
	 */
	iter = bch2_trans_get_iter(trans, BTREE_ID_extents, u->k.p,
				   BTREE_ITER_INTENT|BTREE_ITER_NOT_EXTENTS);

	BUG_ON(iter->flags & BTREE_ITER_IS_EXTENTS);
	bch2_trans_update(trans, iter, u, BTREE_TRIGGER_NORUN);
	bch2_trans_iter_put(trans, iter);

	return bch2_trans_commit(trans, NULL, NULL,
				 BTREE_INSERT_NOFAIL|
				 BTREE_INSERT_LAZY_RW);
}

static int inode_backpointer_exists(struct btree_trans *trans,
				    struct bch_inode_unpacked *inode)
{
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	iter = bch2_trans_get_iter(trans, BTREE_ID_dirents,
				   POS(inode->bi_dir, inode->bi_dir_offset), 0);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		goto out;
	if (k.k->type != KEY_TYPE_dirent)
		goto out;

	ret = le64_to_cpu(bkey_s_c_to_dirent(k).v->d_inum) == inode->bi_inum;
out:
	bch2_trans_iter_free(trans, iter);
	return ret;
}

static bool inode_backpointer_matches(struct bkey_s_c_dirent d,
				      struct bch_inode_unpacked *inode)
{
	return d.k->p.inode == inode->bi_dir &&
		d.k->p.offset == inode->bi_dir_offset;
}

/*
 * Walk extents: verify that extents have a corresponding S_ISREG inode, and
 * that i_size an i_sectors are consistent
 */
noinline_for_stack
static int check_extents(struct bch_fs *c)
{
	struct inode_walker w = inode_walker_init();
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_buf prev;
	u64 i_sectors = 0;
	int ret = 0;

	bch2_bkey_buf_init(&prev);
	prev.k->k = KEY(0, 0, 0);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	bch_verbose(c, "checking extents");

	iter = bch2_trans_get_iter(&trans, BTREE_ID_extents,
				   POS(BCACHEFS_ROOT_INO, 0),
				   BTREE_ITER_INTENT);
retry:
	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = bkey_err(k))) {
		if (w.have_inode &&
		    w.cur_inum != k.k->p.inode &&
		    !(w.inode.bi_flags & BCH_INODE_I_SECTORS_DIRTY) &&
		    fsck_err_on(w.inode.bi_sectors != i_sectors, c,
				"inode %llu has incorrect i_sectors: got %llu, should be %llu",
				w.inode.bi_inum,
				w.inode.bi_sectors, i_sectors)) {
			w.inode.bi_sectors = i_sectors;

			ret = write_inode(&trans, &w.inode, w.snapshot);
			if (ret)
				break;
		}

		if (bkey_cmp(prev.k->k.p, bkey_start_pos(k.k)) > 0) {
			char buf1[200];
			char buf2[200];

			bch2_bkey_val_to_text(&PBUF(buf1), c, bkey_i_to_s_c(prev.k));
			bch2_bkey_val_to_text(&PBUF(buf2), c, k);

			if (fsck_err(c, "overlapping extents:\n%s\n%s", buf1, buf2))
				return fix_overlapping_extent(&trans, k, prev.k->k.p) ?: -EINTR;
		}

		ret = walk_inode(&trans, &w, k.k->p.inode);
		if (ret)
			break;

		if (w.first_this_inode)
			i_sectors = 0;

		if (fsck_err_on(!w.have_inode, c,
				"extent type %u for missing inode %llu",
				k.k->type, k.k->p.inode) ||
		    fsck_err_on(w.have_inode &&
				!S_ISREG(w.inode.bi_mode) && !S_ISLNK(w.inode.bi_mode), c,
				"extent type %u for non regular file, inode %llu mode %o",
				k.k->type, k.k->p.inode, w.inode.bi_mode)) {
			bch2_fs_lazy_rw(c);
			return bch2_btree_delete_range_trans(&trans, BTREE_ID_extents,
						       POS(k.k->p.inode, 0),
						       POS(k.k->p.inode, U64_MAX),
						       NULL) ?: -EINTR;
		}

		if (fsck_err_on(w.have_inode &&
				!(w.inode.bi_flags & BCH_INODE_I_SIZE_DIRTY) &&
				k.k->type != KEY_TYPE_reservation &&
				k.k->p.offset > round_up(w.inode.bi_size, block_bytes(c)) >> 9, c,
				"extent type %u offset %llu past end of inode %llu, i_size %llu",
				k.k->type, k.k->p.offset, k.k->p.inode, w.inode.bi_size)) {
			bch2_fs_lazy_rw(c);
			return bch2_btree_delete_range_trans(&trans, BTREE_ID_extents,
					POS(k.k->p.inode, round_up(w.inode.bi_size, block_bytes(c)) >> 9),
					POS(k.k->p.inode, U64_MAX),
					NULL) ?: -EINTR;
		}

		if (bkey_extent_is_allocation(k.k))
			i_sectors += k.k->size;
		bch2_bkey_buf_reassemble(&prev, c, k);

		bch2_btree_iter_advance(iter);
	}
fsck_err:
	if (ret == -EINTR)
		goto retry;
	bch2_trans_iter_put(&trans, iter);
	bch2_bkey_buf_exit(&prev, c);
	return bch2_trans_exit(&trans) ?: ret;
}

/*
 * Walk dirents: verify that they all have a corresponding S_ISDIR inode,
 * validate d_type
 */
noinline_for_stack
static int check_dirents(struct bch_fs *c)
{
	struct inode_walker w = inode_walker_init();
	struct bch_hash_info hash_info;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	char buf[200];
	unsigned nr_subdirs = 0;
	int ret = 0;

	bch_verbose(c, "checking dirents");

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_dirents,
				   POS(BCACHEFS_ROOT_INO, 0), 0);
retry:
	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = bkey_err(k))) {
		struct bkey_s_c_dirent d;
		struct bch_inode_unpacked target;
		u32 target_snapshot;
		bool have_target;
		bool backpointer_exists = true;
		u64 d_inum;

		if (w.have_inode &&
		    w.cur_inum != k.k->p.inode &&
		    fsck_err_on(w.inode.bi_nlink != nr_subdirs, c,
				"directory %llu with wrong i_nlink: got %u, should be %u",
				w.inode.bi_inum, w.inode.bi_nlink, nr_subdirs)) {
			w.inode.bi_nlink = nr_subdirs;
			ret = write_inode(&trans, &w.inode, w.snapshot);
			if (ret)
				break;
		}

		ret = walk_inode(&trans, &w, k.k->p.inode);
		if (ret)
			break;

		if (w.first_this_inode)
			nr_subdirs = 0;

		if (fsck_err_on(!w.have_inode, c,
				"dirent in nonexisting directory:\n%s",
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf)) ||
		    fsck_err_on(!S_ISDIR(w.inode.bi_mode), c,
				"dirent in non directory inode type %u:\n%s",
				mode_to_type(w.inode.bi_mode),
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf))) {
			ret = lockrestart_do(&trans,
					bch2_btree_delete_at(&trans, iter, 0));
			if (ret)
				goto err;
			goto next;
		}

		if (!w.have_inode)
			goto next;

		if (w.first_this_inode)
			hash_info = bch2_hash_info_init(c, &w.inode);

		ret = hash_check_key(&trans, bch2_dirent_hash_desc,
				     &hash_info, iter, k);
		if (ret > 0) {
			ret = 0;
			goto next;
		}
		if (ret)
			goto fsck_err;

		if (k.k->type != KEY_TYPE_dirent)
			goto next;

		d = bkey_s_c_to_dirent(k);
		d_inum = le64_to_cpu(d.v->d_inum);

		ret = lookup_inode(&trans, d_inum, &target, &target_snapshot);
		if (ret && ret != -ENOENT)
			break;

		have_target = !ret;
		ret = 0;

		if (fsck_err_on(!have_target, c,
				"dirent points to missing inode:\n%s",
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf))) {
			ret = remove_dirent(&trans, d.k->p);
			if (ret)
				goto err;
			goto next;
		}

		if (!have_target)
			goto next;

		if (!target.bi_dir &&
		    !target.bi_dir_offset) {
			target.bi_dir		= k.k->p.inode;
			target.bi_dir_offset	= k.k->p.offset;

			ret = write_inode(&trans, &target, target_snapshot);
			if (ret)
				goto err;
		}

		if (!inode_backpointer_matches(d, &target)) {
			ret = inode_backpointer_exists(&trans, &target);
			if (ret < 0)
				goto err;

			backpointer_exists = ret;
			ret = 0;

			if (fsck_err_on(S_ISDIR(target.bi_mode) &&
					backpointer_exists, c,
					"directory %llu with multiple links",
					target.bi_inum)) {
				ret = remove_dirent(&trans, d.k->p);
				if (ret)
					goto err;
				continue;
			}

			if (fsck_err_on(backpointer_exists &&
					!target.bi_nlink, c,
					"inode %llu has multiple links but i_nlink 0",
					d_inum)) {
				target.bi_nlink++;
				target.bi_flags &= ~BCH_INODE_UNLINKED;

				ret = write_inode(&trans, &target, target_snapshot);
				if (ret)
					goto err;
			}

			if (fsck_err_on(!backpointer_exists, c,
					"inode %llu has wrong backpointer:\n"
					"got       %llu:%llu\n"
					"should be %llu:%llu",
					d_inum,
					target.bi_dir,
					target.bi_dir_offset,
					k.k->p.inode,
					k.k->p.offset)) {
				target.bi_dir		= k.k->p.inode;
				target.bi_dir_offset	= k.k->p.offset;

				ret = write_inode(&trans, &target, target_snapshot);
				if (ret)
					goto err;
			}
		}

		if (fsck_err_on(d.v->d_type != mode_to_type(target.bi_mode), c,
				"incorrect d_type: should be %u:\n%s",
				mode_to_type(target.bi_mode),
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf))) {
			struct bkey_i_dirent *n;

			n = kmalloc(bkey_bytes(d.k), GFP_KERNEL);
			if (!n) {
				ret = -ENOMEM;
				goto err;
			}

			bkey_reassemble(&n->k_i, d.s_c);
			n->v.d_type = mode_to_type(target.bi_mode);

			ret = __bch2_trans_do(&trans, NULL, NULL,
					      BTREE_INSERT_NOFAIL|
					      BTREE_INSERT_LAZY_RW,
				(bch2_trans_update(&trans, iter, &n->k_i, 0), 0));
			kfree(n);
			if (ret)
				goto err;

		}

		nr_subdirs += d.v->d_type == DT_DIR;
next:
		bch2_btree_iter_advance(iter);
	}
err:
fsck_err:
	if (ret == -EINTR)
		goto retry;

	bch2_trans_iter_put(&trans, iter);
	return bch2_trans_exit(&trans) ?: ret;
}

/*
 * Walk xattrs: verify that they all have a corresponding inode
 */
noinline_for_stack
static int check_xattrs(struct bch_fs *c)
{
	struct inode_walker w = inode_walker_init();
	struct bch_hash_info hash_info;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret = 0;

	bch_verbose(c, "checking xattrs");

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_xattrs,
				   POS(BCACHEFS_ROOT_INO, 0), 0);
retry:
	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = bkey_err(k))) {
		ret = walk_inode(&trans, &w, k.k->p.inode);
		if (ret)
			break;

		if (fsck_err_on(!w.have_inode, c,
				"xattr for missing inode %llu",
				k.k->p.inode)) {
			ret = bch2_btree_delete_at(&trans, iter, 0);
			if (ret)
				break;
			continue;
		}

		if (w.first_this_inode && w.have_inode)
			hash_info = bch2_hash_info_init(c, &w.inode);

		ret = hash_check_key(&trans, bch2_xattr_hash_desc,
				     &hash_info, iter, k);
		if (ret)
			break;

		bch2_btree_iter_advance(iter);
	}
fsck_err:
	if (ret == -EINTR)
		goto retry;

	bch2_trans_iter_put(&trans, iter);
	return bch2_trans_exit(&trans) ?: ret;
}

/* Get root directory, create if it doesn't exist: */
static int check_root(struct bch_fs *c, struct bch_inode_unpacked *root_inode)
{
	struct bkey_inode_buf packed;
	u32 snapshot;
	int ret;

	bch_verbose(c, "checking root directory");

	ret = bch2_trans_do(c, NULL, NULL, 0,
		lookup_inode(&trans, BCACHEFS_ROOT_INO, root_inode, &snapshot));
	if (ret && ret != -ENOENT)
		return ret;

	if (fsck_err_on(ret, c, "root directory missing"))
		goto create_root;

	if (fsck_err_on(!S_ISDIR(root_inode->bi_mode), c,
			"root inode not a directory"))
		goto create_root;

	return 0;
fsck_err:
	return ret;
create_root:
	bch2_inode_init(c, root_inode, 0, 0, S_IFDIR|0755,
			0, NULL);
	root_inode->bi_inum = BCACHEFS_ROOT_INO;

	bch2_inode_pack(c, &packed, root_inode);

	return bch2_btree_insert(c, BTREE_ID_inodes, &packed.inode.k_i,
				 NULL, NULL,
				 BTREE_INSERT_NOFAIL|
				 BTREE_INSERT_LAZY_RW);
}

/* Get lost+found, create if it doesn't exist: */
static int check_lostfound(struct bch_fs *c,
			   struct bch_inode_unpacked *root_inode,
			   struct bch_inode_unpacked *lostfound_inode)
{
	struct qstr lostfound = QSTR("lost+found");
	struct bch_hash_info root_hash_info =
		bch2_hash_info_init(c, root_inode);
	u64 inum;
	u32 snapshot;
	int ret;

	bch_verbose(c, "checking lost+found");

	inum = bch2_dirent_lookup(c, BCACHEFS_ROOT_INO, &root_hash_info,
				 &lostfound);
	if (!inum) {
		bch_notice(c, "creating lost+found");
		goto create_lostfound;
	}

	ret = bch2_trans_do(c, NULL, NULL, 0,
		lookup_inode(&trans, inum, lostfound_inode, &snapshot));
	if (ret && ret != -ENOENT)
		return ret;

	if (fsck_err_on(ret, c, "lost+found missing"))
		goto create_lostfound;

	if (fsck_err_on(!S_ISDIR(lostfound_inode->bi_mode), c,
			"lost+found inode not a directory"))
		goto create_lostfound;

	return 0;
fsck_err:
	return ret;
create_lostfound:
	bch2_inode_init_early(c, lostfound_inode);

	ret = bch2_trans_do(c, NULL, NULL,
			    BTREE_INSERT_NOFAIL|
			    BTREE_INSERT_LAZY_RW,
		bch2_create_trans(&trans,
				  BCACHEFS_ROOT_INO, root_inode,
				  lostfound_inode, &lostfound,
				  0, 0, S_IFDIR|0700, 0, NULL, NULL));
	if (ret)
		bch_err(c, "error creating lost+found: %i", ret);

	return ret;
}

struct pathbuf {
	size_t		nr;
	size_t		size;

	struct pathbuf_entry {
		u64	inum;
	}		*entries;
};

static int path_down(struct pathbuf *p, u64 inum)
{
	if (p->nr == p->size) {
		size_t new_size = max_t(size_t, 256UL, p->size * 2);
		void *n = krealloc(p->entries,
				   new_size * sizeof(p->entries[0]),
				   GFP_KERNEL);
		if (!n) {
			return -ENOMEM;
		}

		p->entries = n;
		p->size = new_size;
	};

	p->entries[p->nr++] = (struct pathbuf_entry) {
		.inum = inum,
	};
	return 0;
}

static int check_path(struct btree_trans *trans,
		      struct bch_inode_unpacked *lostfound,
		      struct pathbuf *p,
		      struct bch_inode_unpacked *inode)
{
	struct bch_fs *c = trans->c;
	u32 snapshot;
	size_t i;
	int ret = 0;

	p->nr = 0;

	while (inode->bi_inum != BCACHEFS_ROOT_INO) {
		ret = lockrestart_do(trans,
			inode_backpointer_exists(trans, inode));
		if (ret < 0)
			break;

		if (!ret) {
			if (fsck_err(c,  "unreachable inode %llu, type %u nlink %u backptr %llu:%llu",
				     inode->bi_inum,
				     mode_to_type(inode->bi_mode),
				     inode->bi_nlink,
				     inode->bi_dir,
				     inode->bi_dir_offset))
				ret = reattach_inode(trans, lostfound, inode->bi_inum);
			break;
		}
		ret = 0;

		if (!S_ISDIR(inode->bi_mode))
			break;

		ret = path_down(p, inode->bi_inum);
		if (ret) {
			bch_err(c, "memory allocation failure");
			return ret;
		}

		for (i = 0; i < p->nr; i++) {
			if (inode->bi_dir != p->entries[i].inum)
				continue;

			/* XXX print path */
			if (!fsck_err(c, "directory structure loop"))
				return 0;

			ret = lockrestart_do(trans,
					 remove_backpointer(trans, inode));
			if (ret) {
				bch_err(c, "error removing dirent: %i", ret);
				break;
			}

			ret = reattach_inode(trans, lostfound, inode->bi_inum);
			break;
		}

		ret = lockrestart_do(trans,
				lookup_inode(trans, inode->bi_dir, inode, &snapshot));
		if (ret) {
			/* Should have been caught in dirents pass */
			bch_err(c, "error looking up parent directory: %i", ret);
			break;
		}
	}
fsck_err:
	if (ret)
		bch_err(c, "%s: err %i", __func__, ret);
	return ret;
}

/*
 * Check for unreachable inodes, as well as loops in the directory structure:
 * After check_dirents(), if an inode backpointer doesn't exist that means it's
 * unreachable:
 */
static int check_directory_structure(struct bch_fs *c,
				     struct bch_inode_unpacked *lostfound)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bch_inode_unpacked u;
	struct pathbuf path = { 0, 0, NULL };
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_inodes, POS_MIN, 0, k, ret) {
		if (k.k->type != KEY_TYPE_inode)
			continue;

		ret = bch2_inode_unpack(bkey_s_c_to_inode(k), &u);
		if (ret) {
			/* Should have been caught earlier in fsck: */
			bch_err(c, "error unpacking inode %llu: %i", k.k->p.offset, ret);
			break;
		}

		ret = check_path(&trans, lostfound, &path, &u);
		if (ret)
			break;
	}
	bch2_trans_iter_put(&trans, iter);

	BUG_ON(ret == -EINTR);

	kfree(path.entries);

	return bch2_trans_exit(&trans) ?: ret;
}

struct nlink {
	u32	count;
};

typedef GENRADIX(struct nlink) nlink_table;

static void inc_link(struct bch_fs *c, nlink_table *links,
		     u64 range_start, u64 *range_end, u64 inum)
{
	struct nlink *link;

	if (inum < range_start || inum >= *range_end)
		return;

	if (inum - range_start >= SIZE_MAX / sizeof(struct nlink)) {
		*range_end = inum;
		return;
	}

	link = genradix_ptr_alloc(links, inum - range_start, GFP_KERNEL);
	if (!link) {
		bch_verbose(c, "allocation failed during fsck - will need another pass");
		*range_end = inum;
		return;
	}

	link->count++;
}

noinline_for_stack
static int bch2_gc_walk_dirents(struct bch_fs *c, nlink_table *links,
			       u64 range_start, u64 *range_end)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_s_c_dirent d;
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_dirents, POS_MIN, 0, k, ret) {
		switch (k.k->type) {
		case KEY_TYPE_dirent:
			d = bkey_s_c_to_dirent(k);

			if (d.v->d_type != DT_DIR)
				inc_link(c, links, range_start, range_end,
					 le64_to_cpu(d.v->d_inum));
			break;
		}

		bch2_trans_cond_resched(&trans);
	}
	bch2_trans_iter_put(&trans, iter);

	ret = bch2_trans_exit(&trans) ?: ret;
	if (ret)
		bch_err(c, "error in fsck: btree error %i while walking dirents", ret);

	return ret;
}

static int check_inode_nlink(struct btree_trans *trans,
			     struct bch_inode_unpacked *lostfound_inode,
			     struct btree_iter *iter,
			     struct bkey_s_c_inode inode,
			     unsigned nlink)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked u;
	int ret = 0;

	/*
	 * Backpointer and directory structure checks are sufficient for
	 * directories, since they can't have hardlinks:
	 */
	if (S_ISDIR(le16_to_cpu(inode.v->bi_mode)))
		return 0;

	if (!nlink) {
		bch_err(c, "no links found to inode %llu", inode.k->p.offset);
		return -EINVAL;
	}

	ret = bch2_inode_unpack(inode, &u);

	/* Should never happen, checked by bch2_inode_invalid: */
	if (bch2_fs_inconsistent_on(ret, c,
			 "error unpacking inode %llu in fsck",
			 inode.k->p.inode))
		return ret;

	if (fsck_err_on(bch2_inode_nlink_get(&u) != nlink, c,
			"inode %llu has wrong i_nlink (type %u i_nlink %u, should be %u)",
			u.bi_inum, mode_to_type(u.bi_mode),
			bch2_inode_nlink_get(&u), nlink)) {
		bch2_inode_nlink_set(&u, nlink);

		ret = __bch2_trans_do(trans, NULL, NULL,
				      BTREE_INSERT_NOFAIL|
				      BTREE_INSERT_LAZY_RW,
				bch2_inode_write(trans, iter, &u));
		if (ret)
			bch_err(c, "error in fsck: error %i updating inode", ret);
	}
fsck_err:
	return ret;
}

noinline_for_stack
static int bch2_gc_walk_inodes(struct bch_fs *c,
			       struct bch_inode_unpacked *lostfound_inode,
			       nlink_table *links,
			       u64 range_start, u64 range_end)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct nlink *link;
	int ret = 0;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_inodes,
			   POS(0, range_start), 0, k, ret) {
		if (!k.k || k.k->p.offset >= range_end)
			break;

		if (k.k->type != KEY_TYPE_inode)
			continue;

		link = genradix_ptr(links, k.k->p.offset - range_start);
		ret = check_inode_nlink(&trans, lostfound_inode, iter,
					bkey_s_c_to_inode(k), link ? link->count : 0);
		if (ret)
			break;

	}
	bch2_trans_iter_put(&trans, iter);
	bch2_trans_exit(&trans);

	if (ret)
		bch_err(c, "error in fsck: btree error %i while walking inodes", ret);

	return ret;
}

noinline_for_stack
static int check_nlinks(struct bch_fs *c,
			      struct bch_inode_unpacked *lostfound_inode)
{
	nlink_table links;
	u64 this_iter_range_start, next_iter_range_start = 0;
	int ret = 0;

	bch_verbose(c, "checking inode nlinks");

	genradix_init(&links);

	do {
		this_iter_range_start = next_iter_range_start;
		next_iter_range_start = U64_MAX;

		ret = bch2_gc_walk_dirents(c, &links,
					  this_iter_range_start,
					  &next_iter_range_start);
		if (ret)
			break;

		ret = bch2_gc_walk_inodes(c, lostfound_inode, &links,
					 this_iter_range_start,
					 next_iter_range_start);
		if (ret)
			break;

		genradix_free(&links);
	} while (next_iter_range_start != U64_MAX);

	genradix_free(&links);

	return ret;
}

/*
 * Checks for inconsistencies that shouldn't happen, unless we have a bug.
 * Doesn't fix them yet, mainly because they haven't yet been observed:
 */
int bch2_fsck_full(struct bch_fs *c)
{
	struct bch_inode_unpacked root_inode, lostfound_inode;

	return  check_inodes(c, true) ?:
		check_extents(c) ?:
		check_dirents(c) ?:
		check_xattrs(c) ?:
		check_root(c, &root_inode) ?:
		check_lostfound(c, &root_inode, &lostfound_inode) ?:
		check_directory_structure(c, &lostfound_inode) ?:
		check_nlinks(c, &lostfound_inode);
}

int bch2_fsck_walk_inodes_only(struct bch_fs *c)
{
	return check_inodes(c, false);
}
