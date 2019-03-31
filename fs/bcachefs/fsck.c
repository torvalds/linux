// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_update.h"
#include "dirent.h"
#include "error.h"
#include "fs.h"
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

	for_each_btree_key(trans, iter, BTREE_ID_EXTENTS, POS(inum, 0), 0, k) {
		if (k.k->p.inode != inum)
			break;

		if (bkey_extent_is_allocation(k.k))
			sectors += k.k->size;
	}

	return bch2_trans_iter_free(trans, iter) ?: sectors;
}

static int remove_dirent(struct btree_trans *trans,
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
	buf = kmalloc(name.len + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, dirent.v->d_name, name.len);
	buf[name.len] = '\0';
	name.name = buf;

	/* Unlock so we don't deadlock, after copying name: */
	bch2_btree_trans_unlock(trans);

	ret = bch2_inode_find_by_inum(c, dir_inum, &dir_inode);
	if (ret) {
		bch_err(c, "remove_dirent: err %i looking up directory inode", ret);
		goto err;
	}

	dir_hash_info = bch2_hash_info_init(c, &dir_inode);

	ret = bch2_dirent_delete(c, dir_inum, &dir_hash_info, &name, NULL);
	if (ret)
		bch_err(c, "remove_dirent: err %i deleting dirent", ret);
err:
	kfree(buf);
	return ret;
}

static int reattach_inode(struct bch_fs *c,
			  struct bch_inode_unpacked *lostfound_inode,
			  u64 inum)
{
	struct bch_hash_info lostfound_hash_info =
		bch2_hash_info_init(c, lostfound_inode);
	struct bkey_inode_buf packed;
	char name_buf[20];
	struct qstr name;
	int ret;

	snprintf(name_buf, sizeof(name_buf), "%llu", inum);
	name = (struct qstr) QSTR(name_buf);

	lostfound_inode->bi_nlink++;

	bch2_inode_pack(&packed, lostfound_inode);

	ret = bch2_btree_insert(c, BTREE_ID_INODES, &packed.inode.k_i,
				NULL, NULL,
				BTREE_INSERT_NOFAIL|
				BTREE_INSERT_LAZY_RW);
	if (ret) {
		bch_err(c, "error %i reattaching inode %llu while updating lost+found",
			ret, inum);
		return ret;
	}

	ret = bch2_dirent_create(c, lostfound_inode->bi_inum,
				 &lostfound_hash_info,
				 DT_DIR, &name, inum, NULL,
				 BTREE_INSERT_NOFAIL|
				 BTREE_INSERT_LAZY_RW);
	if (ret) {
		bch_err(c, "error %i reattaching inode %llu while creating new dirent",
			ret, inum);
		return ret;
	}
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
		int ret = bch2_inode_find_by_inum_trans(trans, inum,
							&w->inode);

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

struct hash_check {
	struct bch_hash_info	info;

	/* start of current chain of hash collisions: */
	struct btree_iter	*chain;

	/* next offset in current chain of hash collisions: */
	u64			chain_end;
};

static void hash_check_init(struct hash_check *h)
{
	h->chain = NULL;
}

static void hash_stop_chain(struct btree_trans *trans,
			    struct hash_check *h)
{
	if (h->chain)
		bch2_trans_iter_free(trans, h->chain);
	h->chain = NULL;
}

static void hash_check_set_inode(struct btree_trans *trans,
				 struct hash_check *h,
				 const struct bch_inode_unpacked *bi)
{
	h->info = bch2_hash_info_init(trans->c, bi);
	hash_stop_chain(trans, h);
}

static int hash_redo_key(const struct bch_hash_desc desc,
			 struct btree_trans *trans, struct hash_check *h,
			 struct btree_iter *k_iter, struct bkey_s_c k,
			 u64 hashed)
{
	struct bkey_i *tmp;
	int ret = 0;

	tmp = kmalloc(bkey_bytes(k.k), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	bkey_reassemble(tmp, k);

	ret = bch2_btree_delete_at(trans, k_iter, 0);
	if (ret)
		goto err;

	bch2_hash_set(trans, desc, &h->info, k_iter->pos.inode,
		      tmp, BCH_HASH_SET_MUST_CREATE);
	ret = bch2_trans_commit(trans, NULL, NULL,
				BTREE_INSERT_NOFAIL|
				BTREE_INSERT_LAZY_RW);
err:
	kfree(tmp);
	return ret;
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
				  BTREE_INSERT_ATOMIC|
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_LAZY_RW);
	if (ret == -EINTR) {
		ret = bch2_btree_iter_traverse(iter);
		if (!ret)
			goto retry;
	}

	return ret;
}

static int hash_check_duplicates(struct btree_trans *trans,
			const struct bch_hash_desc desc, struct hash_check *h,
			struct btree_iter *k_iter, struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter;
	struct bkey_s_c k2;
	char buf[200];
	int ret = 0;

	if (!bkey_cmp(h->chain->pos, k_iter->pos))
		return 0;

	iter = bch2_trans_copy_iter(trans, h->chain);
	BUG_ON(IS_ERR(iter));

	for_each_btree_key_continue(iter, 0, k2) {
		if (bkey_cmp(k2.k->p, k.k->p) >= 0)
			break;

		if (fsck_err_on(k2.k->type == desc.key_type &&
				!desc.cmp_bkey(k, k2), c,
				"duplicate hash table keys:\n%s",
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf))) {
			ret = fsck_hash_delete_at(trans, desc, &h->info, k_iter);
			if (ret)
				return ret;
			ret = 1;
			break;
		}
	}
fsck_err:
	bch2_trans_iter_free(trans, iter);
	return ret;
}

static void hash_set_chain_start(struct btree_trans *trans,
			const struct bch_hash_desc desc,
			struct hash_check *h,
			struct btree_iter *k_iter, struct bkey_s_c k)
{
	bool hole = (k.k->type != KEY_TYPE_whiteout &&
		     k.k->type != desc.key_type);

	if (hole || k.k->p.offset > h->chain_end + 1)
		hash_stop_chain(trans, h);

	if (!hole) {
		if (!h->chain) {
			h->chain = bch2_trans_copy_iter(trans, k_iter);
			BUG_ON(IS_ERR(h->chain));
		}

		h->chain_end = k.k->p.offset;
	}
}

static bool key_has_correct_hash(struct btree_trans *trans,
			const struct bch_hash_desc desc,
			struct hash_check *h,
			struct btree_iter *k_iter, struct bkey_s_c k)
{
	u64 hash;

	hash_set_chain_start(trans, desc, h, k_iter, k);

	if (k.k->type != desc.key_type)
		return true;

	hash = desc.hash_bkey(&h->info, k);

	return hash >= h->chain->pos.offset &&
		hash <= k.k->p.offset;
}

static int hash_check_key(struct btree_trans *trans,
			const struct bch_hash_desc desc, struct hash_check *h,
			struct btree_iter *k_iter, struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	char buf[200];
	u64 hashed;
	int ret = 0;

	hash_set_chain_start(trans, desc, h, k_iter, k);

	if (k.k->type != desc.key_type)
		return 0;

	hashed = desc.hash_bkey(&h->info, k);

	if (fsck_err_on(hashed < h->chain->pos.offset ||
			hashed > k.k->p.offset, c,
			"hash table key at wrong offset: btree %u, %llu, "
			"hashed to %llu chain starts at %llu\n%s",
			desc.btree_id, k.k->p.offset,
			hashed, h->chain->pos.offset,
			(bch2_bkey_val_to_text(&PBUF(buf), c,
					       k), buf))) {
		ret = hash_redo_key(desc, trans, h, k_iter, k, hashed);
		if (ret) {
			bch_err(c, "hash_redo_key err %i", ret);
			return ret;
		}
		return 1;
	}

	ret = hash_check_duplicates(trans, desc, h, k_iter, k);
fsck_err:
	return ret;
}

static int check_dirent_hash(struct btree_trans *trans, struct hash_check *h,
			     struct btree_iter *iter, struct bkey_s_c *k)
{
	struct bch_fs *c = trans->c;
	struct bkey_i_dirent *d = NULL;
	int ret = -EINVAL;
	char buf[200];
	unsigned len;
	u64 hash;

	if (key_has_correct_hash(trans, bch2_dirent_hash_desc, h, iter, *k))
		return 0;

	len = bch2_dirent_name_bytes(bkey_s_c_to_dirent(*k));
	BUG_ON(!len);

	memcpy(buf, bkey_s_c_to_dirent(*k).v->d_name, len);
	buf[len] = '\0';

	d = kmalloc(bkey_bytes(k->k), GFP_KERNEL);
	if (!d) {
		bch_err(c, "memory allocation failure");
		return -ENOMEM;
	}

	bkey_reassemble(&d->k_i, *k);

	do {
		--len;
		if (!len)
			goto err_redo;

		d->k.u64s = BKEY_U64s + dirent_val_u64s(len);

		BUG_ON(bkey_val_bytes(&d->k) <
		       offsetof(struct bch_dirent, d_name) + len);

		memset(d->v.d_name + len, 0,
		       bkey_val_bytes(&d->k) -
		       offsetof(struct bch_dirent, d_name) - len);

		hash = bch2_dirent_hash_desc.hash_bkey(&h->info,
						bkey_i_to_s_c(&d->k_i));
	} while (hash < h->chain->pos.offset ||
		 hash > k->k->p.offset);

	if (fsck_err(c, "dirent with junk at end, was %s (%zu) now %s (%u)",
		     buf, strlen(buf), d->v.d_name, len)) {
		bch2_trans_update(trans, BTREE_INSERT_ENTRY(iter, &d->k_i));

		ret = bch2_trans_commit(trans, NULL, NULL,
					BTREE_INSERT_NOFAIL|
					BTREE_INSERT_LAZY_RW);
		if (ret)
			goto err;

		*k = bch2_btree_iter_peek(iter);

		BUG_ON(k->k->type != KEY_TYPE_dirent);
	}
err:
fsck_err:
	kfree(d);
	return ret;
err_redo:
	hash = bch2_dirent_hash_desc.hash_bkey(&h->info, *k);

	if (fsck_err(c, "cannot fix dirent by removing trailing garbage %s (%zu)\n"
		     "hash table key at wrong offset: btree %u, offset %llu, "
		     "hashed to %llu chain starts at %llu\n%s",
		     buf, strlen(buf), BTREE_ID_DIRENTS,
		     k->k->p.offset, hash, h->chain->pos.offset,
		     (bch2_bkey_val_to_text(&PBUF(buf), c,
					    *k), buf))) {
		ret = hash_redo_key(bch2_dirent_hash_desc, trans,
				    h, iter, *k, hash);
		if (ret)
			bch_err(c, "hash_redo_key err %i", ret);
		else
			ret = 1;
	}

	goto err;
}

static int bch2_inode_truncate(struct bch_fs *c, u64 inode_nr, u64 new_size)
{
	return bch2_btree_delete_range(c, BTREE_ID_EXTENTS,
			POS(inode_nr, round_up(new_size, block_bytes(c)) >> 9),
			POS(inode_nr + 1, 0), NULL);
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
	u64 i_sectors;
	int ret = 0;

	bch2_trans_init(&trans, c);
	bch2_trans_preload_iters(&trans);

	bch_verbose(c, "checking extents");

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS,
				   POS(BCACHEFS_ROOT_INO, 0), 0);
retry:
	for_each_btree_key_continue(iter, 0, k) {
		ret = walk_inode(&trans, &w, k.k->p.inode);
		if (ret)
			break;

		if (fsck_err_on(!w.have_inode, c,
			"extent type %u for missing inode %llu",
			k.k->type, k.k->p.inode) ||
		    fsck_err_on(w.have_inode &&
			!S_ISREG(w.inode.bi_mode) && !S_ISLNK(w.inode.bi_mode), c,
			"extent type %u for non regular file, inode %llu mode %o",
			k.k->type, k.k->p.inode, w.inode.bi_mode)) {
			bch2_trans_unlock(&trans);

			ret = bch2_inode_truncate(c, k.k->p.inode, 0);
			if (ret)
				goto err;
			continue;
		}

		if (fsck_err_on(w.first_this_inode &&
			w.have_inode &&
			!(w.inode.bi_flags & BCH_INODE_I_SECTORS_DIRTY) &&
			w.inode.bi_sectors !=
			(i_sectors = bch2_count_inode_sectors(&trans, w.cur_inum)),
			c, "i_sectors wrong: got %llu, should be %llu",
			w.inode.bi_sectors, i_sectors)) {
			struct bkey_inode_buf p;

			w.inode.bi_sectors = i_sectors;

			bch2_trans_unlock(&trans);

			bch2_inode_pack(&p, &w.inode);

			ret = bch2_btree_insert(c, BTREE_ID_INODES,
						&p.inode.k_i, NULL, NULL,
						BTREE_INSERT_NOFAIL|
						BTREE_INSERT_LAZY_RW);
			if (ret) {
				bch_err(c, "error in fs gc: error %i "
					"updating inode", ret);
				goto err;
			}

			/* revalidate iterator: */
			k = bch2_btree_iter_peek(iter);
		}

		if (fsck_err_on(w.have_inode &&
			!(w.inode.bi_flags & BCH_INODE_I_SIZE_DIRTY) &&
			k.k->type != KEY_TYPE_reservation &&
			k.k->p.offset > round_up(w.inode.bi_size, PAGE_SIZE) >> 9, c,
			"extent type %u offset %llu past end of inode %llu, i_size %llu",
			k.k->type, k.k->p.offset, k.k->p.inode, w.inode.bi_size)) {
			bch2_trans_unlock(&trans);

			ret = bch2_inode_truncate(c, k.k->p.inode,
						  w.inode.bi_size);
			if (ret)
				goto err;
			continue;
		}
	}
err:
fsck_err:
	if (ret == -EINTR)
		goto retry;
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
	struct hash_check h;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	unsigned name_len;
	char buf[200];
	int ret = 0;

	bch_verbose(c, "checking dirents");

	bch2_trans_init(&trans, c);
	bch2_trans_preload_iters(&trans);

	hash_check_init(&h);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_DIRENTS,
				   POS(BCACHEFS_ROOT_INO, 0), 0);
retry:
	for_each_btree_key_continue(iter, 0, k) {
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
			continue;
		}

		if (w.first_this_inode && w.have_inode)
			hash_check_set_inode(&trans, &h, &w.inode);

		ret = check_dirent_hash(&trans, &h, iter, &k);
		if (ret > 0) {
			ret = 0;
			continue;
		}
		if (ret)
			goto fsck_err;

		if (ret)
			goto fsck_err;

		if (k.k->type != KEY_TYPE_dirent)
			continue;

		d = bkey_s_c_to_dirent(k);
		d_inum = le64_to_cpu(d.v->d_inum);

		name_len = bch2_dirent_name_bytes(d);

		if (fsck_err_on(!name_len, c, "empty dirent") ||
		    fsck_err_on(name_len == 1 &&
				!memcmp(d.v->d_name, ".", 1), c,
				". dirent") ||
		    fsck_err_on(name_len == 2 &&
				!memcmp(d.v->d_name, "..", 2), c,
				".. dirent") ||
		    fsck_err_on(name_len == 2 &&
				!memcmp(d.v->d_name, "..", 2), c,
				".. dirent") ||
		    fsck_err_on(memchr(d.v->d_name, '/', name_len), c,
				"dirent name has invalid chars")) {
			ret = remove_dirent(&trans, d);
			if (ret)
				goto err;
			continue;
		}

		if (fsck_err_on(d_inum == d.k->p.inode, c,
				"dirent points to own directory:\n%s",
				(bch2_bkey_val_to_text(&PBUF(buf), c,
						       k), buf))) {
			ret = remove_dirent(&trans, d);
			if (ret)
				goto err;
			continue;
		}

		ret = bch2_inode_find_by_inum_trans(&trans, d_inum, &target);
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
			continue;
		}

		if (fsck_err_on(have_target &&
				d.v->d_type !=
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

			bch2_trans_update(&trans,
				BTREE_INSERT_ENTRY(iter, &n->k_i));

			ret = bch2_trans_commit(&trans, NULL, NULL,
						BTREE_INSERT_NOFAIL|
						BTREE_INSERT_LAZY_RW);
			kfree(n);
			if (ret)
				goto err;

		}
	}

	hash_stop_chain(&trans, &h);
err:
fsck_err:
	if (ret == -EINTR)
		goto retry;

	return bch2_trans_exit(&trans) ?: ret;
}

/*
 * Walk xattrs: verify that they all have a corresponding inode
 */
noinline_for_stack
static int check_xattrs(struct bch_fs *c)
{
	struct inode_walker w = inode_walker_init();
	struct hash_check h;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret = 0;

	bch_verbose(c, "checking xattrs");

	hash_check_init(&h);

	bch2_trans_init(&trans, c);
	bch2_trans_preload_iters(&trans);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_XATTRS,
				   POS(BCACHEFS_ROOT_INO, 0), 0);
retry:
	for_each_btree_key_continue(iter, 0, k) {
		ret = walk_inode(&trans, &w, k.k->p.inode);
		if (ret)
			break;

		if (fsck_err_on(!w.have_inode, c,
				"xattr for missing inode %llu",
				k.k->p.inode)) {
			ret = bch2_btree_delete_at(&trans, iter, 0);
			if (ret)
				goto err;
			continue;
		}

		if (w.first_this_inode && w.have_inode)
			hash_check_set_inode(&trans, &h, &w.inode);

		ret = hash_check_key(&trans, bch2_xattr_hash_desc,
				     &h, iter, k);
		if (ret)
			goto fsck_err;
	}
err:
fsck_err:
	if (ret == -EINTR)
		goto retry;
	return bch2_trans_exit(&trans) ?: ret;
}

/* Get root directory, create if it doesn't exist: */
static int check_root(struct bch_fs *c, struct bch_inode_unpacked *root_inode)
{
	struct bkey_inode_buf packed;
	int ret;

	bch_verbose(c, "checking root directory");

	ret = bch2_inode_find_by_inum(c, BCACHEFS_ROOT_INO, root_inode);
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
	bch2_inode_init(c, root_inode, 0, 0, S_IFDIR|S_IRWXU|S_IRUGO|S_IXUGO,
			0, NULL);
	root_inode->bi_inum = BCACHEFS_ROOT_INO;

	bch2_inode_pack(&packed, root_inode);

	return bch2_btree_insert(c, BTREE_ID_INODES, &packed.inode.k_i,
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
	struct bkey_inode_buf packed;
	u64 inum;
	int ret;

	bch_verbose(c, "checking lost+found");

	inum = bch2_dirent_lookup(c, BCACHEFS_ROOT_INO, &root_hash_info,
				 &lostfound);
	if (!inum) {
		bch_notice(c, "creating lost+found");
		goto create_lostfound;
	}

	ret = bch2_inode_find_by_inum(c, inum, lostfound_inode);
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
	root_inode->bi_nlink++;

	bch2_inode_pack(&packed, root_inode);

	ret = bch2_btree_insert(c, BTREE_ID_INODES, &packed.inode.k_i,
				NULL, NULL,
				BTREE_INSERT_NOFAIL|
				BTREE_INSERT_LAZY_RW);
	if (ret)
		return ret;

	bch2_inode_init(c, lostfound_inode, 0, 0, S_IFDIR|S_IRWXU|S_IRUGO|S_IXUGO,
			0, root_inode);

	ret = bch2_inode_create(c, lostfound_inode, BLOCKDEV_INODE_MAX, 0,
			       &c->unused_inode_hint);
	if (ret)
		return ret;

	ret = bch2_dirent_create(c, BCACHEFS_ROOT_INO, &root_hash_info, DT_DIR,
				 &lostfound, lostfound_inode->bi_inum, NULL,
				 BTREE_INSERT_NOFAIL|
				 BTREE_INSERT_LAZY_RW);
	if (ret)
		return ret;

	return 0;
}

struct inode_bitmap {
	unsigned long	*bits;
	size_t		size;
};

static inline bool inode_bitmap_test(struct inode_bitmap *b, size_t nr)
{
	return nr < b->size ? test_bit(nr, b->bits) : false;
}

static inline int inode_bitmap_set(struct inode_bitmap *b, size_t nr)
{
	if (nr >= b->size) {
		size_t new_size = max_t(size_t, max_t(size_t,
					PAGE_SIZE * 8,
					b->size * 2),
					nr + 1);
		void *n;

		new_size = roundup_pow_of_two(new_size);
		n = krealloc(b->bits, new_size / 8, GFP_KERNEL|__GFP_ZERO);
		if (!n) {
			return -ENOMEM;
		}

		b->bits = n;
		b->size = new_size;
	}

	__set_bit(nr, b->bits);
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
	struct inode_bitmap dirs_done = { NULL, 0 };
	struct pathbuf path = { 0, 0, NULL };
	struct pathbuf_entry *e;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_s_c_dirent dirent;
	bool had_unreachable;
	u64 d_inum;
	int ret = 0;

	bch2_trans_init(&trans, c);
	bch2_trans_preload_iters(&trans);

	bch_verbose(c, "checking directory structure");

	/* DFS: */
restart_dfs:
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

		for_each_btree_key(&trans, iter, BTREE_ID_DIRENTS,
				   POS(e->inum, e->offset + 1), 0, k) {
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
		ret = bch2_trans_iter_free(&trans, iter);
		if (ret) {
			bch_err(c, "btree error %i in fsck", ret);
			goto err;
		}
up:
		path.nr--;
	}

	iter = bch2_trans_get_iter(&trans, BTREE_ID_INODES, POS_MIN, 0);
retry:
	for_each_btree_key_continue(iter, 0, k) {
		if (k.k->type != KEY_TYPE_inode)
			continue;

		if (!S_ISDIR(le16_to_cpu(bkey_s_c_to_inode(k).v->bi_mode)))
			continue;

		ret = bch2_empty_dir_trans(&trans, k.k->p.inode);
		if (ret == -EINTR)
			goto retry;
		if (!ret)
			continue;

		if (fsck_err_on(!inode_bitmap_test(&dirs_done, k.k->p.inode), c,
				"unreachable directory found (inum %llu)",
				k.k->p.inode)) {
			bch2_btree_trans_unlock(&trans);

			ret = reattach_inode(c, lostfound_inode, k.k->p.inode);
			if (ret) {
				goto err;
			}

			had_unreachable = true;
		}
	}
	ret = bch2_trans_iter_free(&trans, iter);
	if (ret)
		goto err;

	if (had_unreachable) {
		bch_info(c, "reattached unreachable directories, restarting pass to check for loops");
		kfree(dirs_done.bits);
		kfree(path.entries);
		memset(&dirs_done, 0, sizeof(dirs_done));
		memset(&path, 0, sizeof(path));
		goto restart_dfs;
	}
err:
fsck_err:
	ret = bch2_trans_exit(&trans) ?: ret;
	kfree(dirs_done.bits);
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

	link = genradix_ptr_alloc(links, inum - range_start, GFP_KERNEL);
	if (!link) {
		bch_verbose(c, "allocation failed during fs gc - will need another pass");
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

	bch2_trans_init(&trans, c);
	bch2_trans_preload_iters(&trans);

	inc_link(c, links, range_start, range_end, BCACHEFS_ROOT_INO, false);

	for_each_btree_key(&trans, iter, BTREE_ID_DIRENTS, POS_MIN, 0, k) {
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
	ret = bch2_trans_exit(&trans);
	if (ret)
		bch_err(c, "error in fs gc: btree error %i while walking dirents", ret);

	return ret;
}

static int check_inode_nlink(struct bch_fs *c,
			     struct bch_inode_unpacked *lostfound_inode,
			     struct bch_inode_unpacked *u,
			     struct nlink *link,
			     bool *do_update)
{
	u32 i_nlink = u->bi_flags & BCH_INODE_UNLINKED
		? 0
		: u->bi_nlink + nlink_bias(u->bi_mode);
	u32 real_i_nlink =
		link->count * nlink_bias(u->bi_mode) +
		link->dir_count;
	int ret = 0;

	/*
	 * These should have been caught/fixed by earlier passes, we don't
	 * repair them here:
	 */
	if (S_ISDIR(u->bi_mode) && link->count > 1) {
		need_fsck_err(c, "directory %llu with multiple hardlinks: %u",
			      u->bi_inum, link->count);
		return 0;
	}

	if (S_ISDIR(u->bi_mode) && !link->count) {
		need_fsck_err(c, "unreachable directory found (inum %llu)",
			      u->bi_inum);
		return 0;
	}

	if (!S_ISDIR(u->bi_mode) && link->dir_count) {
		need_fsck_err(c, "non directory with subdirectories",
			      u->bi_inum);
		return 0;
	}

	if (!link->count &&
	    !(u->bi_flags & BCH_INODE_UNLINKED) &&
	    (c->sb.features & (1 << BCH_FEATURE_ATOMIC_NLINK))) {
		if (fsck_err(c, "unreachable inode %llu not marked as unlinked (type %u)",
			     u->bi_inum, mode_to_type(u->bi_mode)) ==
		    FSCK_ERR_IGNORE)
			return 0;

		ret = reattach_inode(c, lostfound_inode, u->bi_inum);
		if (ret)
			return ret;

		link->count = 1;
		real_i_nlink = nlink_bias(u->bi_mode) + link->dir_count;
		goto set_i_nlink;
	}

	if (i_nlink < link->count) {
		if (fsck_err(c, "inode %llu i_link too small (%u < %u, type %i)",
			     u->bi_inum, i_nlink, link->count,
			     mode_to_type(u->bi_mode)) == FSCK_ERR_IGNORE)
			return 0;
		goto set_i_nlink;
	}

	if (i_nlink != real_i_nlink &&
	    c->sb.clean) {
		if (fsck_err(c, "filesystem marked clean, "
			     "but inode %llu has wrong i_nlink "
			     "(type %u i_nlink %u, should be %u)",
			     u->bi_inum, mode_to_type(u->bi_mode),
			     i_nlink, real_i_nlink) == FSCK_ERR_IGNORE)
			return 0;
		goto set_i_nlink;
	}

	if (i_nlink != real_i_nlink &&
	    (c->sb.features & (1 << BCH_FEATURE_ATOMIC_NLINK))) {
		if (fsck_err(c, "inode %llu has wrong i_nlink "
			     "(type %u i_nlink %u, should be %u)",
			     u->bi_inum, mode_to_type(u->bi_mode),
			     i_nlink, real_i_nlink) == FSCK_ERR_IGNORE)
			return 0;
		goto set_i_nlink;
	}

	if (real_i_nlink && i_nlink != real_i_nlink)
		bch_verbose(c, "setting inode %llu nlink from %u to %u",
			    u->bi_inum, i_nlink, real_i_nlink);
set_i_nlink:
	if (i_nlink != real_i_nlink) {
		if (real_i_nlink) {
			u->bi_nlink = real_i_nlink - nlink_bias(u->bi_mode);
			u->bi_flags &= ~BCH_INODE_UNLINKED;
		} else {
			u->bi_nlink = 0;
			u->bi_flags |= BCH_INODE_UNLINKED;
		}

		*do_update = true;
	}
fsck_err:
	return ret;
}

static int check_inode(struct btree_trans *trans,
		       struct bch_inode_unpacked *lostfound_inode,
		       struct btree_iter *iter,
		       struct bkey_s_c_inode inode,
		       struct nlink *link)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked u;
	bool do_update = false;
	int ret = 0;

	ret = bch2_inode_unpack(inode, &u);

	bch2_btree_trans_unlock(trans);

	if (bch2_fs_inconsistent_on(ret, c,
			 "error unpacking inode %llu in fsck",
			 inode.k->p.inode))
		return ret;

	if (link) {
		ret = check_inode_nlink(c, lostfound_inode, &u, link,
					&do_update);
		if (ret)
			return ret;
	}

	if (u.bi_flags & BCH_INODE_UNLINKED) {
		fsck_err_on(c->sb.clean, c,
			    "filesystem marked clean, "
			    "but inode %llu unlinked",
			    u.bi_inum);

		bch_verbose(c, "deleting inode %llu", u.bi_inum);

		ret = bch2_inode_rm(c, u.bi_inum);
		if (ret)
			bch_err(c, "error in fs gc: error %i "
				"while deleting inode", ret);
		return ret;
	}

	if (u.bi_flags & BCH_INODE_I_SIZE_DIRTY) {
		fsck_err_on(c->sb.clean, c,
			    "filesystem marked clean, "
			    "but inode %llu has i_size dirty",
			    u.bi_inum);

		bch_verbose(c, "truncating inode %llu", u.bi_inum);

		/*
		 * XXX: need to truncate partial blocks too here - or ideally
		 * just switch units to bytes and that issue goes away
		 */

		ret = bch2_inode_truncate(c, u.bi_inum, u.bi_size);
		if (ret) {
			bch_err(c, "error in fs gc: error %i "
				"truncating inode", ret);
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

	if (u.bi_flags & BCH_INODE_I_SECTORS_DIRTY) {
		s64 sectors;

		fsck_err_on(c->sb.clean, c,
			    "filesystem marked clean, "
			    "but inode %llu has i_sectors dirty",
			    u.bi_inum);

		bch_verbose(c, "recounting sectors for inode %llu",
			    u.bi_inum);

		sectors = bch2_count_inode_sectors(trans, u.bi_inum);
		if (sectors < 0) {
			bch_err(c, "error in fs gc: error %i "
				"recounting inode sectors",
				(int) sectors);
			return sectors;
		}

		u.bi_sectors = sectors;
		u.bi_flags &= ~BCH_INODE_I_SECTORS_DIRTY;
		do_update = true;
	}

	if (do_update) {
		struct bkey_inode_buf p;

		bch2_inode_pack(&p, &u);
		bch2_trans_update(trans, BTREE_INSERT_ENTRY(iter, &p.inode.k_i));

		ret = bch2_trans_commit(trans, NULL, NULL,
					BTREE_INSERT_NOFAIL|
					BTREE_INSERT_LAZY_RW);
		if (ret && ret != -EINTR)
			bch_err(c, "error in fs gc: error %i "
				"updating inode", ret);
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

	bch2_trans_init(&trans, c);
	bch2_trans_preload_iters(&trans);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_INODES,
				   POS(range_start, 0), 0);
	nlinks_iter = genradix_iter_init(links, 0);

	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret2 = bkey_err(k))) {
peek_nlinks:	link = genradix_iter_peek(&nlinks_iter, links);

		if (!link && (!k.k || iter->pos.inode >= range_end))
			break;

		nlinks_pos = range_start + nlinks_iter.pos;
		if (iter->pos.inode > nlinks_pos) {
			/* Should have been caught by dirents pass: */
			need_fsck_err_on(link && link->count, c,
				"missing inode %llu (nlink %u)",
				nlinks_pos, link->count);
			genradix_iter_advance(&nlinks_iter, links);
			goto peek_nlinks;
		}

		if (iter->pos.inode < nlinks_pos || !link)
			link = &zero_links;

		if (k.k && k.k->type == KEY_TYPE_inode) {
			ret = check_inode(&trans, lostfound_inode, iter,
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

		if (nlinks_pos == iter->pos.inode)
			genradix_iter_advance(&nlinks_iter, links);

		bch2_btree_iter_next(iter);
		bch2_trans_cond_resched(&trans);
	}
fsck_err:
	bch2_trans_exit(&trans);

	if (ret2)
		bch_err(c, "error in fs gc: btree error %i while walking inodes", ret2);

	return ret ?: ret2;
}

noinline_for_stack
static int check_inode_nlinks(struct bch_fs *c,
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

noinline_for_stack
static int check_inodes_fast(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_s_c_inode inode;
	int ret = 0, ret2;

	bch2_trans_init(&trans, c);
	bch2_trans_preload_iters(&trans);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_INODES,
				   POS_MIN, 0);

	for_each_btree_key_continue(iter, 0, k) {
		if (k.k->type != KEY_TYPE_inode)
			continue;

		inode = bkey_s_c_to_inode(k);

		if (inode.v->bi_flags &
		    (BCH_INODE_I_SIZE_DIRTY|
		     BCH_INODE_I_SECTORS_DIRTY|
		     BCH_INODE_UNLINKED)) {
			ret = check_inode(&trans, NULL, iter, inode, NULL);
			BUG_ON(ret == -EINTR);
			if (ret)
				break;
		}
	}

	ret2 = bch2_trans_exit(&trans);

	return ret ?: ret2;
}

/*
 * Checks for inconsistencies that shouldn't happen, unless we have a bug.
 * Doesn't fix them yet, mainly because they haven't yet been observed:
 */
static int bch2_fsck_full(struct bch_fs *c)
{
	struct bch_inode_unpacked root_inode, lostfound_inode;
	int ret;

	bch_verbose(c, "starting fsck:");
	ret =   check_extents(c) ?:
		check_dirents(c) ?:
		check_xattrs(c) ?:
		check_root(c, &root_inode) ?:
		check_lostfound(c, &root_inode, &lostfound_inode) ?:
		check_directory_structure(c, &lostfound_inode) ?:
		check_inode_nlinks(c, &lostfound_inode);

	bch2_flush_fsck_errs(c);
	bch_verbose(c, "fsck done");

	return ret;
}

static int bch2_fsck_inode_nlink(struct bch_fs *c)
{
	struct bch_inode_unpacked root_inode, lostfound_inode;
	int ret;

	bch_verbose(c, "checking inode link counts:");
	ret =   check_root(c, &root_inode) ?:
		check_lostfound(c, &root_inode, &lostfound_inode) ?:
		check_inode_nlinks(c, &lostfound_inode);

	bch2_flush_fsck_errs(c);
	bch_verbose(c, "done");

	return ret;
}

static int bch2_fsck_walk_inodes_only(struct bch_fs *c)
{
	int ret;

	bch_verbose(c, "walking inodes:");
	ret = check_inodes_fast(c);

	bch2_flush_fsck_errs(c);
	bch_verbose(c, "done");

	return ret;
}

int bch2_fsck(struct bch_fs *c)
{
	if (c->opts.fsck)
		return bch2_fsck_full(c);

	if (c->sb.clean)
		return 0;

	return c->sb.features & (1 << BCH_FEATURE_ATOMIC_NLINK)
		? bch2_fsck_walk_inodes_only(c)
		: bch2_fsck_inode_nlink(c);
}
