// SPDX-License-Identifier: GPL-2.0
#ifdef CONFIG_BCACHEFS_TESTS

#include "bcachefs.h"
#include "btree_update.h"
#include "journal_reclaim.h"
#include "snapshot.h"
#include "tests.h"

#include "linux/kthread.h"
#include "linux/random.h"

static void delete_test_keys(struct bch_fs *c)
{
	int ret;

	ret = bch2_btree_delete_range(c, BTREE_ID_extents,
				      SPOS(0, 0, U32_MAX),
				      POS(0, U64_MAX),
				      0, NULL);
	BUG_ON(ret);

	ret = bch2_btree_delete_range(c, BTREE_ID_xattrs,
				      SPOS(0, 0, U32_MAX),
				      POS(0, U64_MAX),
				      0, NULL);
	BUG_ON(ret);
}

/* unit tests */

static int test_delete(struct bch_fs *c, u64 nr)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_i_cookie k;
	int ret;

	bkey_cookie_init(&k.k_i);
	k.k.p.snapshot = U32_MAX;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_xattrs, k.k.p,
			     BTREE_ITER_intent);

	ret = commit_do(trans, NULL, NULL, 0,
		bch2_btree_iter_traverse(&iter) ?:
		bch2_trans_update(trans, &iter, &k.k_i, 0));
	bch_err_msg(c, ret, "update error");
	if (ret)
		goto err;

	pr_info("deleting once");
	ret = commit_do(trans, NULL, NULL, 0,
		bch2_btree_iter_traverse(&iter) ?:
		bch2_btree_delete_at(trans, &iter, 0));
	bch_err_msg(c, ret, "delete error (first)");
	if (ret)
		goto err;

	pr_info("deleting twice");
	ret = commit_do(trans, NULL, NULL, 0,
		bch2_btree_iter_traverse(&iter) ?:
		bch2_btree_delete_at(trans, &iter, 0));
	bch_err_msg(c, ret, "delete error (second)");
	if (ret)
		goto err;
err:
	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	return ret;
}

static int test_delete_written(struct bch_fs *c, u64 nr)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_i_cookie k;
	int ret;

	bkey_cookie_init(&k.k_i);
	k.k.p.snapshot = U32_MAX;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_xattrs, k.k.p,
			     BTREE_ITER_intent);

	ret = commit_do(trans, NULL, NULL, 0,
		bch2_btree_iter_traverse(&iter) ?:
		bch2_trans_update(trans, &iter, &k.k_i, 0));
	bch_err_msg(c, ret, "update error");
	if (ret)
		goto err;

	bch2_trans_unlock(trans);
	bch2_journal_flush_all_pins(&c->journal);

	ret = commit_do(trans, NULL, NULL, 0,
		bch2_btree_iter_traverse(&iter) ?:
		bch2_btree_delete_at(trans, &iter, 0));
	bch_err_msg(c, ret, "delete error");
	if (ret)
		goto err;
err:
	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	return ret;
}

static int test_iterate(struct bch_fs *c, u64 nr)
{
	u64 i;
	int ret = 0;

	delete_test_keys(c);

	pr_info("inserting test keys");

	for (i = 0; i < nr; i++) {
		struct bkey_i_cookie ck;

		bkey_cookie_init(&ck.k_i);
		ck.k.p.offset = i;
		ck.k.p.snapshot = U32_MAX;

		ret = bch2_btree_insert(c, BTREE_ID_xattrs, &ck.k_i, NULL, 0, 0);
		bch_err_msg(c, ret, "insert error");
		if (ret)
			return ret;
	}

	pr_info("iterating forwards");
	i = 0;

	ret = bch2_trans_run(c,
		for_each_btree_key_max(trans, iter, BTREE_ID_xattrs,
					SPOS(0, 0, U32_MAX), POS(0, U64_MAX),
					0, k, ({
			BUG_ON(k.k->p.offset != i++);
			0;
		})));
	bch_err_msg(c, ret, "error iterating forwards");
	if (ret)
		return ret;

	BUG_ON(i != nr);

	pr_info("iterating backwards");

	ret = bch2_trans_run(c,
		for_each_btree_key_reverse(trans, iter, BTREE_ID_xattrs,
				SPOS(0, U64_MAX, U32_MAX), 0, k, ({
			BUG_ON(k.k->p.offset != --i);
			0;
		})));
	bch_err_msg(c, ret, "error iterating backwards");
	if (ret)
		return ret;

	BUG_ON(i);
	return 0;
}

static int test_iterate_extents(struct bch_fs *c, u64 nr)
{
	u64 i;
	int ret = 0;

	delete_test_keys(c);

	pr_info("inserting test extents");

	for (i = 0; i < nr; i += 8) {
		struct bkey_i_cookie ck;

		bkey_cookie_init(&ck.k_i);
		ck.k.p.offset = i + 8;
		ck.k.p.snapshot = U32_MAX;
		ck.k.size = 8;

		ret = bch2_btree_insert(c, BTREE_ID_extents, &ck.k_i, NULL, 0, 0);
		bch_err_msg(c, ret, "insert error");
		if (ret)
			return ret;
	}

	pr_info("iterating forwards");
	i = 0;

	ret = bch2_trans_run(c,
		for_each_btree_key_max(trans, iter, BTREE_ID_extents,
					SPOS(0, 0, U32_MAX), POS(0, U64_MAX),
					0, k, ({
			BUG_ON(bkey_start_offset(k.k) != i);
			i = k.k->p.offset;
			0;
		})));
	bch_err_msg(c, ret, "error iterating forwards");
	if (ret)
		return ret;

	BUG_ON(i != nr);

	pr_info("iterating backwards");

	ret = bch2_trans_run(c,
		for_each_btree_key_reverse(trans, iter, BTREE_ID_extents,
				SPOS(0, U64_MAX, U32_MAX), 0, k, ({
			BUG_ON(k.k->p.offset != i);
			i = bkey_start_offset(k.k);
			0;
		})));
	bch_err_msg(c, ret, "error iterating backwards");
	if (ret)
		return ret;

	BUG_ON(i);
	return 0;
}

static int test_iterate_slots(struct bch_fs *c, u64 nr)
{
	u64 i;
	int ret = 0;

	delete_test_keys(c);

	pr_info("inserting test keys");

	for (i = 0; i < nr; i++) {
		struct bkey_i_cookie ck;

		bkey_cookie_init(&ck.k_i);
		ck.k.p.offset = i * 2;
		ck.k.p.snapshot = U32_MAX;

		ret = bch2_btree_insert(c, BTREE_ID_xattrs, &ck.k_i, NULL, 0, 0);
		bch_err_msg(c, ret, "insert error");
		if (ret)
			return ret;
	}

	pr_info("iterating forwards");
	i = 0;

	ret = bch2_trans_run(c,
		for_each_btree_key_max(trans, iter, BTREE_ID_xattrs,
					  SPOS(0, 0, U32_MAX), POS(0, U64_MAX),
					  0, k, ({
			BUG_ON(k.k->p.offset != i);
			i += 2;
			0;
		})));
	bch_err_msg(c, ret, "error iterating forwards");
	if (ret)
		return ret;

	BUG_ON(i != nr * 2);

	pr_info("iterating forwards by slots");
	i = 0;

	ret = bch2_trans_run(c,
		for_each_btree_key_max(trans, iter, BTREE_ID_xattrs,
					SPOS(0, 0, U32_MAX), POS(0, U64_MAX),
					BTREE_ITER_slots, k, ({
			if (i >= nr * 2)
				break;

			BUG_ON(k.k->p.offset != i);
			BUG_ON(bkey_deleted(k.k) != (i & 1));

			i++;
			0;
		})));
	bch_err_msg(c, ret, "error iterating forwards by slots");
	return ret;
}

static int test_iterate_slots_extents(struct bch_fs *c, u64 nr)
{
	u64 i;
	int ret = 0;

	delete_test_keys(c);

	pr_info("inserting test keys");

	for (i = 0; i < nr; i += 16) {
		struct bkey_i_cookie ck;

		bkey_cookie_init(&ck.k_i);
		ck.k.p.offset = i + 16;
		ck.k.p.snapshot = U32_MAX;
		ck.k.size = 8;

		ret = bch2_btree_insert(c, BTREE_ID_extents, &ck.k_i, NULL, 0, 0);
		bch_err_msg(c, ret, "insert error");
		if (ret)
			return ret;
	}

	pr_info("iterating forwards");
	i = 0;

	ret = bch2_trans_run(c,
		for_each_btree_key_max(trans, iter, BTREE_ID_extents,
					SPOS(0, 0, U32_MAX), POS(0, U64_MAX),
					0, k, ({
			BUG_ON(bkey_start_offset(k.k) != i + 8);
			BUG_ON(k.k->size != 8);
			i += 16;
			0;
		})));
	bch_err_msg(c, ret, "error iterating forwards");
	if (ret)
		return ret;

	BUG_ON(i != nr);

	pr_info("iterating forwards by slots");
	i = 0;

	ret = bch2_trans_run(c,
		for_each_btree_key_max(trans, iter, BTREE_ID_extents,
					SPOS(0, 0, U32_MAX), POS(0, U64_MAX),
					BTREE_ITER_slots, k, ({
			if (i == nr)
				break;
			BUG_ON(bkey_deleted(k.k) != !(i % 16));

			BUG_ON(bkey_start_offset(k.k) != i);
			BUG_ON(k.k->size != 8);
			i = k.k->p.offset;
			0;
		})));
	bch_err_msg(c, ret, "error iterating forwards by slots");
	return ret;
}

/*
 * XXX: we really want to make sure we've got a btree with depth > 0 for these
 * tests
 */
static int test_peek_end(struct bch_fs *c, u64 nr)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_s_c k;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_xattrs,
			     SPOS(0, 0, U32_MAX), 0);

	lockrestart_do(trans, bkey_err(k = bch2_btree_iter_peek_max(&iter, POS(0, U64_MAX))));
	BUG_ON(k.k);

	lockrestart_do(trans, bkey_err(k = bch2_btree_iter_peek_max(&iter, POS(0, U64_MAX))));
	BUG_ON(k.k);

	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	return 0;
}

static int test_peek_end_extents(struct bch_fs *c, u64 nr)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_s_c k;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			     SPOS(0, 0, U32_MAX), 0);

	lockrestart_do(trans, bkey_err(k = bch2_btree_iter_peek_max(&iter, POS(0, U64_MAX))));
	BUG_ON(k.k);

	lockrestart_do(trans, bkey_err(k = bch2_btree_iter_peek_max(&iter, POS(0, U64_MAX))));
	BUG_ON(k.k);

	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	return 0;
}

/* extent unit tests */

static u64 test_version;

static int insert_test_extent(struct bch_fs *c,
			      u64 start, u64 end)
{
	struct bkey_i_cookie k;
	int ret;

	bkey_cookie_init(&k.k_i);
	k.k_i.k.p.offset = end;
	k.k_i.k.p.snapshot = U32_MAX;
	k.k_i.k.size = end - start;
	k.k_i.k.bversion.lo = test_version++;

	ret = bch2_btree_insert(c, BTREE_ID_extents, &k.k_i, NULL, 0, 0);
	bch_err_fn(c, ret);
	return ret;
}

static int __test_extent_overwrite(struct bch_fs *c,
				    u64 e1_start, u64 e1_end,
				    u64 e2_start, u64 e2_end)
{
	int ret;

	ret   = insert_test_extent(c, e1_start, e1_end) ?:
		insert_test_extent(c, e2_start, e2_end);

	delete_test_keys(c);
	return ret;
}

static int test_extent_overwrite_front(struct bch_fs *c, u64 nr)
{
	return  __test_extent_overwrite(c, 0, 64, 0, 32) ?:
		__test_extent_overwrite(c, 8, 64, 0, 32);
}

static int test_extent_overwrite_back(struct bch_fs *c, u64 nr)
{
	return  __test_extent_overwrite(c, 0, 64, 32, 64) ?:
		__test_extent_overwrite(c, 0, 64, 32, 72);
}

static int test_extent_overwrite_middle(struct bch_fs *c, u64 nr)
{
	return __test_extent_overwrite(c, 0, 64, 32, 40);
}

static int test_extent_overwrite_all(struct bch_fs *c, u64 nr)
{
	return  __test_extent_overwrite(c, 32, 64,  0,  64) ?:
		__test_extent_overwrite(c, 32, 64,  0, 128) ?:
		__test_extent_overwrite(c, 32, 64, 32,  64) ?:
		__test_extent_overwrite(c, 32, 64, 32, 128);
}

static int insert_test_overlapping_extent(struct bch_fs *c, u64 inum, u64 start, u32 len, u32 snapid)
{
	struct bkey_i_cookie k;
	int ret;

	bkey_cookie_init(&k.k_i);
	k.k_i.k.p.inode	= inum;
	k.k_i.k.p.offset = start + len;
	k.k_i.k.p.snapshot = snapid;
	k.k_i.k.size = len;

	ret = bch2_trans_commit_do(c, NULL, NULL, 0,
		bch2_btree_insert_nonextent(trans, BTREE_ID_extents, &k.k_i,
					    BTREE_UPDATE_internal_snapshot_node));
	bch_err_fn(c, ret);
	return ret;
}

static int test_extent_create_overlapping(struct bch_fs *c, u64 inum)
{
	return  insert_test_overlapping_extent(c, inum,  0, 16, U32_MAX - 2) ?: /* overwrite entire */
		insert_test_overlapping_extent(c, inum,  2,  8, U32_MAX - 2) ?:
		insert_test_overlapping_extent(c, inum,  4,  4, U32_MAX) ?:
		insert_test_overlapping_extent(c, inum, 32,  8, U32_MAX - 2) ?: /* overwrite front/back */
		insert_test_overlapping_extent(c, inum, 36,  8, U32_MAX) ?:
		insert_test_overlapping_extent(c, inum, 60,  8, U32_MAX - 2) ?:
		insert_test_overlapping_extent(c, inum, 64,  8, U32_MAX);
}

/* snapshot unit tests */

/* Test skipping over keys in unrelated snapshots: */
static int test_snapshot_filter(struct bch_fs *c, u32 snapid_lo, u32 snapid_hi)
{
	struct btree_trans *trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_i_cookie cookie;
	int ret;

	bkey_cookie_init(&cookie.k_i);
	cookie.k.p.snapshot = snapid_hi;
	ret = bch2_btree_insert(c, BTREE_ID_xattrs, &cookie.k_i, NULL, 0, 0);
	if (ret)
		return ret;

	trans = bch2_trans_get(c);
	bch2_trans_iter_init(trans, &iter, BTREE_ID_xattrs,
			     SPOS(0, 0, snapid_lo), 0);
	lockrestart_do(trans, bkey_err(k = bch2_btree_iter_peek_max(&iter, POS(0, U64_MAX))));

	BUG_ON(k.k->p.snapshot != U32_MAX);

	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	return ret;
}

static int test_snapshots(struct bch_fs *c, u64 nr)
{
	struct bkey_i_cookie cookie;
	u32 snapids[2];
	u32 snapid_subvols[2] = { 1, 1 };
	int ret;

	bkey_cookie_init(&cookie.k_i);
	cookie.k.p.snapshot = U32_MAX;
	ret = bch2_btree_insert(c, BTREE_ID_xattrs, &cookie.k_i, NULL, 0, 0);
	if (ret)
		return ret;

	ret = bch2_trans_commit_do(c, NULL, NULL, 0,
		      bch2_snapshot_node_create(trans, U32_MAX,
						snapids,
						snapid_subvols,
						2));
	if (ret)
		return ret;

	if (snapids[0] > snapids[1])
		swap(snapids[0], snapids[1]);

	ret = test_snapshot_filter(c, snapids[0], snapids[1]);
	bch_err_msg(c, ret, "from test_snapshot_filter");
	return ret;
}

/* perf tests */

static u64 test_rand(void)
{
	u64 v;

	get_random_bytes(&v, sizeof(v));
	return v;
}

static int rand_insert(struct bch_fs *c, u64 nr)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct bkey_i_cookie k;
	int ret = 0;
	u64 i;

	for (i = 0; i < nr; i++) {
		bkey_cookie_init(&k.k_i);
		k.k.p.offset = test_rand();
		k.k.p.snapshot = U32_MAX;

		ret = commit_do(trans, NULL, NULL, 0,
			bch2_btree_insert_trans(trans, BTREE_ID_xattrs, &k.k_i, 0));
		if (ret)
			break;
	}

	bch2_trans_put(trans);
	return ret;
}

static int rand_insert_multi(struct bch_fs *c, u64 nr)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct bkey_i_cookie k[8];
	int ret = 0;
	unsigned j;
	u64 i;

	for (i = 0; i < nr; i += ARRAY_SIZE(k)) {
		for (j = 0; j < ARRAY_SIZE(k); j++) {
			bkey_cookie_init(&k[j].k_i);
			k[j].k.p.offset = test_rand();
			k[j].k.p.snapshot = U32_MAX;
		}

		ret = commit_do(trans, NULL, NULL, 0,
			bch2_btree_insert_trans(trans, BTREE_ID_xattrs, &k[0].k_i, 0) ?:
			bch2_btree_insert_trans(trans, BTREE_ID_xattrs, &k[1].k_i, 0) ?:
			bch2_btree_insert_trans(trans, BTREE_ID_xattrs, &k[2].k_i, 0) ?:
			bch2_btree_insert_trans(trans, BTREE_ID_xattrs, &k[3].k_i, 0) ?:
			bch2_btree_insert_trans(trans, BTREE_ID_xattrs, &k[4].k_i, 0) ?:
			bch2_btree_insert_trans(trans, BTREE_ID_xattrs, &k[5].k_i, 0) ?:
			bch2_btree_insert_trans(trans, BTREE_ID_xattrs, &k[6].k_i, 0) ?:
			bch2_btree_insert_trans(trans, BTREE_ID_xattrs, &k[7].k_i, 0));
		if (ret)
			break;
	}

	bch2_trans_put(trans);
	return ret;
}

static int rand_lookup(struct bch_fs *c, u64 nr)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;
	u64 i;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_xattrs,
			     SPOS(0, 0, U32_MAX), 0);

	for (i = 0; i < nr; i++) {
		bch2_btree_iter_set_pos(&iter, SPOS(0, test_rand(), U32_MAX));

		lockrestart_do(trans, bkey_err(k = bch2_btree_iter_peek(&iter)));
		ret = bkey_err(k);
		if (ret)
			break;
	}

	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	return ret;
}

static int rand_mixed_trans(struct btree_trans *trans,
			    struct btree_iter *iter,
			    struct bkey_i_cookie *cookie,
			    u64 i, u64 pos)
{
	struct bkey_s_c k;
	int ret;

	bch2_btree_iter_set_pos(iter, SPOS(0, pos, U32_MAX));

	k = bch2_btree_iter_peek(iter);
	ret = bkey_err(k);
	bch_err_msg(trans->c, ret, "lookup error");
	if (ret)
		return ret;

	if (!(i & 3) && k.k) {
		bkey_cookie_init(&cookie->k_i);
		cookie->k.p = iter->pos;
		ret = bch2_trans_update(trans, iter, &cookie->k_i, 0);
	}

	return ret;
}

static int rand_mixed(struct bch_fs *c, u64 nr)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_i_cookie cookie;
	int ret = 0;
	u64 i, rand;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_xattrs,
			     SPOS(0, 0, U32_MAX), 0);

	for (i = 0; i < nr; i++) {
		rand = test_rand();
		ret = commit_do(trans, NULL, NULL, 0,
			rand_mixed_trans(trans, &iter, &cookie, i, rand));
		if (ret)
			break;
	}

	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	return ret;
}

static int __do_delete(struct btree_trans *trans, struct bpos pos)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_xattrs, pos,
			     BTREE_ITER_intent);
	k = bch2_btree_iter_peek_max(&iter, POS(0, U64_MAX));
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!k.k)
		goto err;

	ret = bch2_btree_delete_at(trans, &iter, 0);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int rand_delete(struct bch_fs *c, u64 nr)
{
	struct btree_trans *trans = bch2_trans_get(c);
	int ret = 0;
	u64 i;

	for (i = 0; i < nr; i++) {
		struct bpos pos = SPOS(0, test_rand(), U32_MAX);

		ret = commit_do(trans, NULL, NULL, 0,
			__do_delete(trans, pos));
		if (ret)
			break;
	}

	bch2_trans_put(trans);
	return ret;
}

static int seq_insert(struct bch_fs *c, u64 nr)
{
	struct bkey_i_cookie insert;

	bkey_cookie_init(&insert.k_i);

	return bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_xattrs,
					SPOS(0, 0, U32_MAX),
					BTREE_ITER_slots|BTREE_ITER_intent, k,
					NULL, NULL, 0, ({
			if (iter.pos.offset >= nr)
				break;
			insert.k.p = iter.pos;
			bch2_trans_update(trans, &iter, &insert.k_i, 0);
		})));
}

static int seq_lookup(struct bch_fs *c, u64 nr)
{
	return bch2_trans_run(c,
		for_each_btree_key_max(trans, iter, BTREE_ID_xattrs,
				  SPOS(0, 0, U32_MAX), POS(0, U64_MAX),
				  0, k,
		0));
}

static int seq_overwrite(struct bch_fs *c, u64 nr)
{
	return bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_xattrs,
					SPOS(0, 0, U32_MAX),
					BTREE_ITER_intent, k,
					NULL, NULL, 0, ({
			struct bkey_i_cookie u;

			bkey_reassemble(&u.k_i, k);
			bch2_trans_update(trans, &iter, &u.k_i, 0);
		})));
}

static int seq_delete(struct bch_fs *c, u64 nr)
{
	return bch2_btree_delete_range(c, BTREE_ID_xattrs,
				      SPOS(0, 0, U32_MAX),
				      POS(0, U64_MAX),
				      0, NULL);
}

typedef int (*perf_test_fn)(struct bch_fs *, u64);

struct test_job {
	struct bch_fs			*c;
	u64				nr;
	unsigned			nr_threads;
	perf_test_fn			fn;

	atomic_t			ready;
	wait_queue_head_t		ready_wait;

	atomic_t			done;
	struct completion		done_completion;

	u64				start;
	u64				finish;
	int				ret;
};

static int btree_perf_test_thread(void *data)
{
	struct test_job *j = data;
	int ret;

	if (atomic_dec_and_test(&j->ready)) {
		wake_up(&j->ready_wait);
		j->start = sched_clock();
	} else {
		wait_event(j->ready_wait, !atomic_read(&j->ready));
	}

	ret = j->fn(j->c, div64_u64(j->nr, j->nr_threads));
	if (ret) {
		bch_err(j->c, "%ps: error %s", j->fn, bch2_err_str(ret));
		j->ret = ret;
	}

	if (atomic_dec_and_test(&j->done)) {
		j->finish = sched_clock();
		complete(&j->done_completion);
	}

	return 0;
}

int bch2_btree_perf_test(struct bch_fs *c, const char *testname,
			 u64 nr, unsigned nr_threads)
{
	struct test_job j = { .c = c, .nr = nr, .nr_threads = nr_threads };
	char name_buf[20];
	struct printbuf nr_buf = PRINTBUF;
	struct printbuf per_sec_buf = PRINTBUF;
	unsigned i;
	u64 time;

	if (nr == 0 || nr_threads == 0) {
		pr_err("nr of iterations or threads is not allowed to be 0");
		return -EINVAL;
	}

	atomic_set(&j.ready, nr_threads);
	init_waitqueue_head(&j.ready_wait);

	atomic_set(&j.done, nr_threads);
	init_completion(&j.done_completion);

#define perf_test(_test)				\
	if (!strcmp(testname, #_test)) j.fn = _test

	perf_test(rand_insert);
	perf_test(rand_insert_multi);
	perf_test(rand_lookup);
	perf_test(rand_mixed);
	perf_test(rand_delete);

	perf_test(seq_insert);
	perf_test(seq_lookup);
	perf_test(seq_overwrite);
	perf_test(seq_delete);

	/* a unit test, not a perf test: */
	perf_test(test_delete);
	perf_test(test_delete_written);
	perf_test(test_iterate);
	perf_test(test_iterate_extents);
	perf_test(test_iterate_slots);
	perf_test(test_iterate_slots_extents);
	perf_test(test_peek_end);
	perf_test(test_peek_end_extents);

	perf_test(test_extent_overwrite_front);
	perf_test(test_extent_overwrite_back);
	perf_test(test_extent_overwrite_middle);
	perf_test(test_extent_overwrite_all);
	perf_test(test_extent_create_overlapping);

	perf_test(test_snapshots);

	if (!j.fn) {
		pr_err("unknown test %s", testname);
		return -EINVAL;
	}

	//pr_info("running test %s:", testname);

	if (nr_threads == 1)
		btree_perf_test_thread(&j);
	else
		for (i = 0; i < nr_threads; i++)
			kthread_run(btree_perf_test_thread, &j,
				    "bcachefs perf test[%u]", i);

	while (wait_for_completion_interruptible(&j.done_completion))
		;

	time = j.finish - j.start;

	scnprintf(name_buf, sizeof(name_buf), "%s:", testname);
	prt_human_readable_u64(&nr_buf, nr);
	prt_human_readable_u64(&per_sec_buf, div64_u64(nr * NSEC_PER_SEC, time));
	printk(KERN_INFO "%-12s %s with %u threads in %5llu sec, %5llu nsec per iter, %5s per sec\n",
		name_buf, nr_buf.buf, nr_threads,
		div_u64(time, NSEC_PER_SEC),
		div_u64(time * nr_threads, nr),
		per_sec_buf.buf);
	printbuf_exit(&per_sec_buf);
	printbuf_exit(&nr_buf);
	return j.ret;
}

#endif /* CONFIG_BCACHEFS_TESTS */
