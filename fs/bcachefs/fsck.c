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

static int __remove_dirent(struct btree_trans *trans,
			   struct bkey_s_c_dirent dirent)
{
	struct bch_fs *c = trans->c;
	struct qstr name;
	struct bch_inode_unpacked dir_inode;
	struct bch_hash_info dir_hash_info;
	u64 dir_inum = dirent.k->p.inode;
	int ret;
	char *buf;

	name.len = bch2_dirent_name_bytes(dirent);
	buf = bch2_trans_kmalloc(trans, name.len + 1);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	memcpy(buf, dirent.v->d_name, name.len);
	buf[name.len] = '\0';
	name.name = buf;

	ret = __bch2_inode_find_by_inum_trans(trans, dir_inum, &dir_inode, 0);
	if (ret && ret != -EINTR)
		bch_err(c, "remove_dirent: err %i looking up directory inode", ret);
	if (ret)
		return ret;

	dir_hash_info = bch2_hash_info_init(c, &dir_inode);

	ret = bch2_hash_delete(trans, bch2_dirent_hash_desc,
			       &dir_hash_info, dir_inum, &name);
	if (ret && ret != -EINTR)
		bch_err(c, "remove_dirent: err %i deleting dirent", ret);
	if (ret)
		return ret;

	return 0;
}

static int remove_dirent(struct btree_trans *trans,
			 struct bkey_s_c_dirent dirent)
{
	return __bch2_trans_do(trans, NULL, NULL,
			       BTREE_INSERT_NOFAIL|
			       BTREE_INSERT_LAZY_RW,
			       __remove_dirent(trans, dirent));
}

static int reattach_inode(struct bch_fs *c,
			  struct bch_inode_unpacked *lostfound_inode,
			  u64 inum)
{
	struct bch_inode_unpacked dir_u, inode_u;
	char name_buf[20];
	struct qstr name;
	int ret;

	snprintf(name_buf, sizeof(name_buf), "%llu", inum);
	name = (struct qstr) QSTR(name_buf);

	ret = bch2_trans_do(c, NULL, NULL,
			    BTREE_INSERT_LAZY_RW,
		bch2_link_trans(&trans, lostfound_inode->bi_inum,
				inum, &dir_u, &inode_u, &name));
	if (ret)
		bch_err(c, "error %i reattaching inode %llu", ret, inum);

	return ret;
}

struct inode_walker {
	bool			first_this_inode;
	bool			have_inode;
	u64			cur_inum;
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
		int ret = __bch2_inode_find_by_inum_trans(trans, inum,
							  &w->inode, 0);

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

	if (!S_ISDIR(u.bi_mode) &&
	    u.bi_nlink &&
	    !(u.bi_flags & BCH_INODE_BACKPTR_UNTRUSTED) &&
	    (fsck_err_on(c->sb.version >= bcachefs_metadata_version_inode_backpointers, c,
			 "inode missing BCH_INODE_BACKPTR_UNTRUSTED flags") ||
	     c->opts.version_upgrade)) {
		u.bi_flags |= BCH_INODE_BACKPTR_UNTRUSTED;
		do_update = true;
	}

	if (do_update) {
		struct bkey_inode_buf p;

		bch2_inode_pack(c, &p, &u);
		p.inode.k.p = iter->pos;

		ret = __bch2_trans_do(trans, NULL, NULL,
				      BTREE_INSERT_NOFAIL|
				      BTREE_INSERT_LAZY_RW,
			(bch2_trans_update(trans, iter, &p.inode.k_i, 0), 0));
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
			struct btree_iter *inode_iter =
				bch2_trans_get_iter(&trans, BTREE_ID_inodes,
						    POS(0, w.cur_inum),
						    BTREE_ITER_INTENT);

			w.inode.bi_sectors = i_sectors;

			ret = __bch2_trans_do(&trans, NULL, NULL,
					      BTREE_INSERT_NOFAIL|
					      BTREE_INSERT_LAZY_RW,
					      bch2_inode_write(&trans, inode_iter, &w.inode));
			bch2_trans_iter_put(&trans, inode_iter);
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
		bool have_target;
		u64 d_inum;

		ret = walk_inode(&trans, &w, k.k->p.inode);
		if (ret)
			break;

		if (fsck_err_on(!w.have_inode, c,
				"dirent in nonexisting directory:\n%s",
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf)) ||
		    fsck_err_on(!S_ISDIR(w.inode.bi_mode), c,
				"dirent in non directory inode type %u:\n%s",
				mode_to_type(w.inode.bi_mode),
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf))) {
			ret = bch2_btree_delete_at(&trans, iter, 0);
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

		ret = __bch2_inode_find_by_inum_trans(&trans, d_inum, &target, 0);
		if (ret && ret != -ENOENT)
			break;

		have_target = !ret;
		ret = 0;

		if (fsck_err_on(!have_target, c,
				"dirent points to missing inode:\n%s",
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf))) {
			ret = remove_dirent(&trans, d);
			if (ret)
				goto err;
			goto next;
		}

		if (!have_target)
			goto next;

		if (!target.bi_nlink &&
		    !(target.bi_flags & BCH_INODE_BACKPTR_UNTRUSTED) &&
		    (target.bi_dir != k.k->p.inode ||
		     target.bi_dir_offset != k.k->p.offset) &&
		    (fsck_err_on(c->sb.version >= bcachefs_metadata_version_inode_backpointers, c,
				 "inode %llu has wrong backpointer:\n"
				 "got       %llu:%llu\n"
				 "should be %llu:%llu",
				 d_inum,
				 target.bi_dir,
				 target.bi_dir_offset,
				 k.k->p.inode,
				 k.k->p.offset) ||
		     c->opts.version_upgrade)) {
			struct bkey_inode_buf p;

			target.bi_dir		= k.k->p.inode;
			target.bi_dir_offset	= k.k->p.offset;
			bch2_trans_unlock(&trans);

			bch2_inode_pack(c, &p, &target);

			ret = bch2_btree_insert(c, BTREE_ID_inodes,
						&p.inode.k_i, NULL, NULL,
						BTREE_INSERT_NOFAIL|
						BTREE_INSERT_LAZY_RW);
			if (ret) {
				bch_err(c, "error in fsck: error %i updating inode", ret);
				goto err;
			}
			continue;
		}

		if (fsck_err_on(d.v->d_type !=
				mode_to_type(target.bi_mode), c,
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
	int ret;

	bch_verbose(c, "checking root directory");

	ret = bch2_trans_do(c, NULL, NULL, 0,
		__bch2_inode_find_by_inum_trans(&trans, BCACHEFS_ROOT_INO,
						root_inode, 0));
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
	int ret;

	bch_verbose(c, "checking lost+found");

	inum = bch2_dirent_lookup(c, BCACHEFS_ROOT_INO, &root_hash_info,
				 &lostfound);
	if (!inum) {
		bch_notice(c, "creating lost+found");
		goto create_lostfound;
	}

	ret = bch2_trans_do(c, NULL, NULL, 0,
		__bch2_inode_find_by_inum_trans(&trans, inum, lostfound_inode, 0));
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

typedef GENRADIX(unsigned long) inode_bitmap;

static inline bool inode_bitmap_test(inode_bitmap *b, size_t nr)
{
	unsigned long *w = genradix_ptr(b, nr / BITS_PER_LONG);
	return w ? test_bit(nr & (BITS_PER_LONG - 1), w) : false;
}

static inline int inode_bitmap_set(inode_bitmap *b, size_t nr)
{
	unsigned long *w = genradix_ptr_alloc(b, nr / BITS_PER_LONG, GFP_KERNEL);

	if (!w)
		return -ENOMEM;

	*w |= 1UL << (nr & (BITS_PER_LONG - 1));
	return 0;
}

struct pathbuf {
	size_t		nr;
	size_t		size;

	struct pathbuf_entry {
		u64	inum;
		u64	offset;
	}		*entries;
};

static int path_down(struct pathbuf *p, u64 inum)
{
	if (p->nr == p->size) {
		size_t new_size = max_t(size_t, 256UL, p->size * 2);
		void *n = krealloc(p->entries,
				   new_size * sizeof(p->entries[0]),
				   GFP_KERNEL);
		if (!n)
			return -ENOMEM;

		p->entries = n;
		p->size = new_size;
	};

	p->entries[p->nr++] = (struct pathbuf_entry) {
		.inum = inum,
		.offset = 0,
	};
	return 0;
}

noinline_for_stack
static int check_directory_structure(struct bch_fs *c,
				     struct bch_inode_unpacked *lostfound_inode)
{
	inode_bitmap dirs_done;
	struct pathbuf path = { 0, 0, NULL };
	struct pathbuf_entry *e;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_s_c_dirent dirent;
	bool had_unreachable;
	u64 d_inum;
	int ret = 0;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	bch_verbose(c, "checking directory structure");

	/* DFS: */
restart_dfs:
	genradix_init(&dirs_done);
	had_unreachable = false;

	ret = inode_bitmap_set(&dirs_done, BCACHEFS_ROOT_INO);
	if (ret) {
		bch_err(c, "memory allocation failure in inode_bitmap_set()");
		goto err;
	}

	ret = path_down(&path, BCACHEFS_ROOT_INO);
	if (ret)
		goto err;

	while (path.nr) {
next:
		e = &path.entries[path.nr - 1];

		if (e->offset == U64_MAX)
			goto up;

		for_each_btree_key(&trans, iter, BTREE_ID_dirents,
				   POS(e->inum, e->offset + 1), 0, k, ret) {
			if (k.k->p.inode != e->inum)
				break;

			e->offset = k.k->p.offset;

			if (k.k->type != KEY_TYPE_dirent)
				continue;

			dirent = bkey_s_c_to_dirent(k);

			if (dirent.v->d_type != DT_DIR)
				continue;

			d_inum = le64_to_cpu(dirent.v->d_inum);

			if (fsck_err_on(inode_bitmap_test(&dirs_done, d_inum), c,
					"directory %llu has multiple hardlinks",
					d_inum)) {
				ret = remove_dirent(&trans, dirent);
				if (ret)
					goto err;
				continue;
			}

			ret = inode_bitmap_set(&dirs_done, d_inum);
			if (ret) {
				bch_err(c, "memory allocation failure in inode_bitmap_set()");
				goto err;
			}

			ret = path_down(&path, d_inum);
			if (ret) {
				goto err;
			}

			ret = bch2_trans_iter_free(&trans, iter);
			if (ret) {
				bch_err(c, "btree error %i in fsck", ret);
				goto err;
			}
			goto next;
		}
		ret = bch2_trans_iter_free(&trans, iter) ?: ret;
		if (ret) {
			bch_err(c, "btree error %i in fsck", ret);
			goto err;
		}
up:
		path.nr--;
	}

	iter = bch2_trans_get_iter(&trans, BTREE_ID_inodes, POS_MIN, 0);
retry:
	for_each_btree_key_continue(iter, 0, k, ret) {
		if (k.k->type != KEY_TYPE_inode)
			continue;

		if (!S_ISDIR(le16_to_cpu(bkey_s_c_to_inode(k).v->bi_mode)))
			continue;

		ret = bch2_empty_dir_trans(&trans, k.k->p.inode);
		if (ret == -EINTR)
			goto retry;
		if (!ret)
			continue;

		if (fsck_err_on(!inode_bitmap_test(&dirs_done, k.k->p.offset), c,
				"unreachable directory found (inum %llu)",
				k.k->p.offset)) {
			bch2_trans_unlock(&trans);

			ret = reattach_inode(c, lostfound_inode, k.k->p.offset);
			if (ret) {
				goto err;
			}

			had_unreachable = true;
		}
	}
	bch2_trans_iter_free(&trans, iter);
	if (ret)
		goto err;

	if (had_unreachable) {
		bch_info(c, "reattached unreachable directories, restarting pass to check for loops");
		genradix_free(&dirs_done);
		kfree(path.entries);
		memset(&dirs_done, 0, sizeof(dirs_done));
		memset(&path, 0, sizeof(path));
		goto restart_dfs;
	}
err:
fsck_err:
	ret = bch2_trans_exit(&trans) ?: ret;
	genradix_free(&dirs_done);
	kfree(path.entries);
	return ret;
}

struct nlink {
	u32	count;
	u32	dir_count;
};

typedef GENRADIX(struct nlink) nlink_table;

static void inc_link(struct bch_fs *c, nlink_table *links,
		     u64 range_start, u64 *range_end,
		     u64 inum, bool dir)
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

	if (dir)
		link->dir_count++;
	else
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
	u64 d_inum;
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	inc_link(c, links, range_start, range_end, BCACHEFS_ROOT_INO, false);

	for_each_btree_key(&trans, iter, BTREE_ID_dirents, POS_MIN, 0, k, ret) {
		switch (k.k->type) {
		case KEY_TYPE_dirent:
			d = bkey_s_c_to_dirent(k);
			d_inum = le64_to_cpu(d.v->d_inum);

			if (d.v->d_type == DT_DIR)
				inc_link(c, links, range_start, range_end,
					 d.k->p.inode, true);

			inc_link(c, links, range_start, range_end,
				 d_inum, false);

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
			     struct nlink *link)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked u;
	u32 i_nlink, real_i_nlink;
	int ret = 0;

	ret = bch2_inode_unpack(inode, &u);
	/* Should never happen, checked by bch2_inode_invalid: */
	if (bch2_fs_inconsistent_on(ret, c,
			 "error unpacking inode %llu in fsck",
			 inode.k->p.inode))
		return ret;

	i_nlink = bch2_inode_nlink_get(&u);
	real_i_nlink = link->count * nlink_bias(u.bi_mode) + link->dir_count;

	/*
	 * These should have been caught/fixed by earlier passes, we don't
	 * repair them here:
	 */
	if (S_ISDIR(u.bi_mode) && link->count > 1) {
		need_fsck_err(c, "directory %llu with multiple hardlinks: %u",
			      u.bi_inum, link->count);
		return 0;
	}

	if (S_ISDIR(u.bi_mode) && !link->count) {
		need_fsck_err(c, "unreachable directory found (inum %llu)",
			      u.bi_inum);
		return 0;
	}

	if (!S_ISDIR(u.bi_mode) && link->dir_count) {
		need_fsck_err(c, "non directory with subdirectories (inum %llu)",
			      u.bi_inum);
		return 0;
	}

	if (!link->count &&
	    !(u.bi_flags & BCH_INODE_UNLINKED) &&
	    (c->sb.features & (1 << BCH_FEATURE_atomic_nlink))) {
		if (fsck_err(c, "unreachable inode %llu not marked as unlinked (type %u)",
			     u.bi_inum, mode_to_type(u.bi_mode)) ==
		    FSCK_ERR_IGNORE)
			return 0;

		ret = reattach_inode(c, lostfound_inode, u.bi_inum);
		if (ret)
			return ret;

		link->count = 1;
		real_i_nlink = nlink_bias(u.bi_mode) + link->dir_count;
		goto set_i_nlink;
	}

	if (i_nlink < link->count) {
		if (fsck_err(c, "inode %llu i_link too small (%u < %u, type %i)",
			     u.bi_inum, i_nlink, link->count,
			     mode_to_type(u.bi_mode)) == FSCK_ERR_IGNORE)
			return 0;
		goto set_i_nlink;
	}

	if (i_nlink != real_i_nlink &&
	    c->sb.clean) {
		if (fsck_err(c, "filesystem marked clean, "
			     "but inode %llu has wrong i_nlink "
			     "(type %u i_nlink %u, should be %u)",
			     u.bi_inum, mode_to_type(u.bi_mode),
			     i_nlink, real_i_nlink) == FSCK_ERR_IGNORE)
			return 0;
		goto set_i_nlink;
	}

	if (i_nlink != real_i_nlink &&
	    (c->sb.features & (1 << BCH_FEATURE_atomic_nlink))) {
		if (fsck_err(c, "inode %llu has wrong i_nlink "
			     "(type %u i_nlink %u, should be %u)",
			     u.bi_inum, mode_to_type(u.bi_mode),
			     i_nlink, real_i_nlink) == FSCK_ERR_IGNORE)
			return 0;
		goto set_i_nlink;
	}

	if (real_i_nlink && i_nlink != real_i_nlink)
		bch_verbose(c, "setting inode %llu nlink from %u to %u",
			    u.bi_inum, i_nlink, real_i_nlink);
set_i_nlink:
	if (i_nlink != real_i_nlink) {
		struct bkey_inode_buf p;

		bch2_inode_nlink_set(&u, real_i_nlink);
		bch2_inode_pack(c, &p, &u);
		p.inode.k.p = iter->pos;

		ret = __bch2_trans_do(trans, NULL, NULL,
				      BTREE_INSERT_NOFAIL|
				      BTREE_INSERT_LAZY_RW,
			(bch2_trans_update(trans, iter, &p.inode.k_i, 0), 0));
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
	struct nlink *link, zero_links = { 0, 0 };
	struct genradix_iter nlinks_iter;
	int ret = 0, ret2 = 0;
	u64 nlinks_pos;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_inodes,
				   POS(0, range_start), 0);
	nlinks_iter = genradix_iter_init(links, 0);

	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret2 = bkey_err(k)) &&
	       iter->pos.offset < range_end) {
peek_nlinks:	link = genradix_iter_peek(&nlinks_iter, links);

		if (!link && (!k.k || iter->pos.offset >= range_end))
			break;

		nlinks_pos = range_start + nlinks_iter.pos;

		if (link && nlinks_pos < iter->pos.offset) {
			/* Should have been caught by dirents pass: */
			need_fsck_err_on(link->count, c,
				"missing inode %llu (nlink %u)",
				nlinks_pos, link->count);
			genradix_iter_advance(&nlinks_iter, links);
			goto peek_nlinks;
		}

		if (!link || nlinks_pos > iter->pos.offset)
			link = &zero_links;

		if (k.k && k.k->type == KEY_TYPE_inode) {
			ret = check_inode_nlink(&trans, lostfound_inode, iter,
						bkey_s_c_to_inode(k), link);
			BUG_ON(ret == -EINTR);
			if (ret)
				break;
		} else {
			/* Should have been caught by dirents pass: */
			need_fsck_err_on(link->count, c,
				"missing inode %llu (nlink %u)",
				nlinks_pos, link->count);
		}

		if (nlinks_pos == iter->pos.offset)
			genradix_iter_advance(&nlinks_iter, links);

		bch2_btree_iter_advance(iter);
		bch2_trans_cond_resched(&trans);
	}
fsck_err:
	bch2_trans_iter_put(&trans, iter);
	bch2_trans_exit(&trans);

	if (ret2)
		bch_err(c, "error in fsck: btree error %i while walking inodes", ret2);

	return ret ?: ret2;
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
