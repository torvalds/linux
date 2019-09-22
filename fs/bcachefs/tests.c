// SPDX-License-Identifier: GPL-2.0
#ifdef CONFIG_BCACHEFS_TESTS

#include "bcachefs.h"
#include "btree_update.h"
#include "journal_reclaim.h"
#include "tests.h"

#include "linux/kthread.h"
#include "linux/random.h"

static void delete_test_keys(struct bch_fs *c)
{
	int ret;

	ret = bch2_btree_delete_range(c, BTREE_ID_EXTENTS,
				      POS(0, 0), POS(0, U64_MAX),
				      NULL);
	BUG_ON(ret);

	ret = bch2_btree_delete_range(c, BTREE_ID_DIRENTS,
				      POS(0, 0), POS(0, U64_MAX),
				      NULL);
	BUG_ON(ret);
}

/* unit tests */

static void test_delete(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_i_cookie k;
	int ret;

	bkey_cookie_init(&k.k_i);

	bch2_trans_init(&trans, c, 0, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_DIRENTS, k.k.p,
				   BTREE_ITER_INTENT);

	ret = bch2_btree_iter_traverse(iter);
	BUG_ON(ret);

	bch2_trans_update(&trans, iter, &k.k_i);
	ret = bch2_trans_commit(&trans, NULL, NULL, 0);
	BUG_ON(ret);

	pr_info("deleting once");
	ret = bch2_btree_delete_at(&trans, iter, 0);
	BUG_ON(ret);

	pr_info("deleting twice");
	ret = bch2_btree_delete_at(&trans, iter, 0);
	BUG_ON(ret);

	bch2_trans_exit(&trans);
}

static void test_delete_written(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_i_cookie k;
	int ret;

	bkey_cookie_init(&k.k_i);

	bch2_trans_init(&trans, c, 0, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_DIRENTS, k.k.p,
				   BTREE_ITER_INTENT);

	ret = bch2_btree_iter_traverse(iter);
	BUG_ON(ret);

	bch2_trans_update(&trans, iter, &k.k_i);
	ret = bch2_trans_commit(&trans, NULL, NULL, 0);
	BUG_ON(ret);

	bch2_journal_flush_all_pins(&c->journal);

	ret = bch2_btree_delete_at(&trans, iter, 0);
	BUG_ON(ret);

	bch2_trans_exit(&trans);
}

static void test_iterate(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 i;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	delete_test_keys(c);

	pr_info("inserting test keys");

	for (i = 0; i < nr; i++) {
		struct bkey_i_cookie k;

		bkey_cookie_init(&k.k_i);
		k.k.p.offset = i;

		ret = bch2_btree_insert(c, BTREE_ID_DIRENTS, &k.k_i,
					NULL, NULL, 0);
		BUG_ON(ret);
	}

	pr_info("iterating forwards");

	i = 0;

	for_each_btree_key(&trans, iter, BTREE_ID_DIRENTS,
			   POS_MIN, 0, k, ret)
		BUG_ON(k.k->p.offset != i++);

	BUG_ON(i != nr);

	pr_info("iterating backwards");

	while (!IS_ERR_OR_NULL((k = bch2_btree_iter_prev(iter)).k))
		BUG_ON(k.k->p.offset != --i);

	BUG_ON(i);

	bch2_trans_exit(&trans);
}

static void test_iterate_extents(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 i;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	delete_test_keys(c);

	pr_info("inserting test extents");

	for (i = 0; i < nr; i += 8) {
		struct bkey_i_cookie k;

		bkey_cookie_init(&k.k_i);
		k.k.p.offset = i + 8;
		k.k.size = 8;

		ret = bch2_btree_insert(c, BTREE_ID_EXTENTS, &k.k_i,
					NULL, NULL, 0);
		BUG_ON(ret);
	}

	pr_info("iterating forwards");

	i = 0;

	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS,
			   POS_MIN, 0, k, ret) {
		BUG_ON(bkey_start_offset(k.k) != i);
		i = k.k->p.offset;
	}

	BUG_ON(i != nr);

	pr_info("iterating backwards");

	while (!IS_ERR_OR_NULL((k = bch2_btree_iter_prev(iter)).k)) {
		BUG_ON(k.k->p.offset != i);
		i = bkey_start_offset(k.k);
	}

	BUG_ON(i);

	bch2_trans_exit(&trans);
}

static void test_iterate_slots(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 i;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	delete_test_keys(c);

	pr_info("inserting test keys");

	for (i = 0; i < nr; i++) {
		struct bkey_i_cookie k;

		bkey_cookie_init(&k.k_i);
		k.k.p.offset = i * 2;

		ret = bch2_btree_insert(c, BTREE_ID_DIRENTS, &k.k_i,
					NULL, NULL, 0);
		BUG_ON(ret);
	}

	pr_info("iterating forwards");

	i = 0;

	for_each_btree_key(&trans, iter, BTREE_ID_DIRENTS, POS_MIN,
			   0, k, ret) {
		BUG_ON(k.k->p.offset != i);
		i += 2;
	}
	bch2_trans_iter_free(&trans, iter);

	BUG_ON(i != nr * 2);

	pr_info("iterating forwards by slots");

	i = 0;

	for_each_btree_key(&trans, iter, BTREE_ID_DIRENTS, POS_MIN,
			   BTREE_ITER_SLOTS, k, ret) {
		BUG_ON(bkey_deleted(k.k) != (i & 1));
		BUG_ON(k.k->p.offset != i++);

		if (i == nr * 2)
			break;
	}

	bch2_trans_exit(&trans);
}

static void test_iterate_slots_extents(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 i;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	delete_test_keys(c);

	pr_info("inserting test keys");

	for (i = 0; i < nr; i += 16) {
		struct bkey_i_cookie k;

		bkey_cookie_init(&k.k_i);
		k.k.p.offset = i + 16;
		k.k.size = 8;

		ret = bch2_btree_insert(c, BTREE_ID_EXTENTS, &k.k_i,
					NULL, NULL, 0);
		BUG_ON(ret);
	}

	pr_info("iterating forwards");

	i = 0;

	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS, POS_MIN,
			   0, k, ret) {
		BUG_ON(bkey_start_offset(k.k) != i + 8);
		BUG_ON(k.k->size != 8);
		i += 16;
	}
	bch2_trans_iter_free(&trans, iter);

	BUG_ON(i != nr);

	pr_info("iterating forwards by slots");

	i = 0;

	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS, POS_MIN,
			   BTREE_ITER_SLOTS, k, ret) {
		BUG_ON(bkey_deleted(k.k) != !(i % 16));

		BUG_ON(bkey_start_offset(k.k) != i);
		BUG_ON(k.k->size != 8);
		i = k.k->p.offset;

		if (i == nr)
			break;
	}

	bch2_trans_exit(&trans);
}

/*
 * XXX: we really want to make sure we've got a btree with depth > 0 for these
 * tests
 */
static void test_peek_end(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;

	bch2_trans_init(&trans, c, 0, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_DIRENTS, POS_MIN, 0);

	k = bch2_btree_iter_peek(iter);
	BUG_ON(k.k);

	k = bch2_btree_iter_peek(iter);
	BUG_ON(k.k);

	bch2_trans_exit(&trans);
}

static void test_peek_end_extents(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;

	bch2_trans_init(&trans, c, 0, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS, POS_MIN, 0);

	k = bch2_btree_iter_peek(iter);
	BUG_ON(k.k);

	k = bch2_btree_iter_peek(iter);
	BUG_ON(k.k);

	bch2_trans_exit(&trans);
}

/* extent unit tests */

u64 test_version;

static void insert_test_extent(struct bch_fs *c,
			       u64 start, u64 end)
{
	struct bkey_i_cookie k;
	int ret;

	//pr_info("inserting %llu-%llu v %llu", start, end, test_version);

	bkey_cookie_init(&k.k_i);
	k.k_i.k.p.offset = end;
	k.k_i.k.size = end - start;
	k.k_i.k.version.lo = test_version++;

	ret = bch2_btree_insert(c, BTREE_ID_EXTENTS, &k.k_i,
				NULL, NULL, 0);
	BUG_ON(ret);
}

static void __test_extent_overwrite(struct bch_fs *c,
				    u64 e1_start, u64 e1_end,
				    u64 e2_start, u64 e2_end)
{
	insert_test_extent(c, e1_start, e1_end);
	insert_test_extent(c, e2_start, e2_end);

	delete_test_keys(c);
}

static void test_extent_overwrite_front(struct bch_fs *c, u64 nr)
{
	__test_extent_overwrite(c, 0, 64, 0, 32);
	__test_extent_overwrite(c, 8, 64, 0, 32);
}

static void test_extent_overwrite_back(struct bch_fs *c, u64 nr)
{
	__test_extent_overwrite(c, 0, 64, 32, 64);
	__test_extent_overwrite(c, 0, 64, 32, 72);
}

static void test_extent_overwrite_middle(struct bch_fs *c, u64 nr)
{
	__test_extent_overwrite(c, 0, 64, 32, 40);
}

static void test_extent_overwrite_all(struct bch_fs *c, u64 nr)
{
	__test_extent_overwrite(c, 32, 64,  0,  64);
	__test_extent_overwrite(c, 32, 64,  0, 128);
	__test_extent_overwrite(c, 32, 64, 32,  64);
	__test_extent_overwrite(c, 32, 64, 32, 128);
}

/* perf tests */

static u64 test_rand(void)
{
	u64 v;
#if 0
	v = prandom_u32_max(U32_MAX);
#else
	get_random_bytes(&v, sizeof(v));
#endif
	return v;
}

static void rand_insert(struct bch_fs *c, u64 nr)
{
	struct bkey_i_cookie k;
	int ret;
	u64 i;

	for (i = 0; i < nr; i++) {
		bkey_cookie_init(&k.k_i);
		k.k.p.offset = test_rand();

		ret = bch2_btree_insert(c, BTREE_ID_DIRENTS, &k.k_i,
					NULL, NULL, 0);
		BUG_ON(ret);
	}
}

static void rand_lookup(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	u64 i;

	bch2_trans_init(&trans, c, 0, 0);

	for (i = 0; i < nr; i++) {
		iter = bch2_trans_get_iter(&trans, BTREE_ID_DIRENTS,
					   POS(0, test_rand()), 0);

		k = bch2_btree_iter_peek(iter);
		bch2_trans_iter_free(&trans, iter);
	}

	bch2_trans_exit(&trans);
}

static void rand_mixed(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;
	u64 i;

	bch2_trans_init(&trans, c, 0, 0);

	for (i = 0; i < nr; i++) {
		iter = bch2_trans_get_iter(&trans, BTREE_ID_DIRENTS,
					   POS(0, test_rand()), 0);

		k = bch2_btree_iter_peek(iter);

		if (!(i & 3) && k.k) {
			struct bkey_i_cookie k;

			bkey_cookie_init(&k.k_i);
			k.k.p = iter->pos;

			bch2_trans_update(&trans, iter, &k.k_i);
			ret = bch2_trans_commit(&trans, NULL, NULL, 0);
			BUG_ON(ret);
		}

		bch2_trans_iter_free(&trans, iter);
	}

	bch2_trans_exit(&trans);
}

static void rand_delete(struct bch_fs *c, u64 nr)
{
	struct bkey_i k;
	int ret;
	u64 i;

	for (i = 0; i < nr; i++) {
		bkey_init(&k.k);
		k.k.p.offset = test_rand();

		ret = bch2_btree_insert(c, BTREE_ID_DIRENTS, &k,
					NULL, NULL, 0);
		BUG_ON(ret);
	}
}

static void seq_insert(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_i_cookie insert;
	int ret;
	u64 i = 0;

	bkey_cookie_init(&insert.k_i);

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_DIRENTS, POS_MIN,
			   BTREE_ITER_SLOTS|BTREE_ITER_INTENT, k, ret) {
		insert.k.p = iter->pos;

		bch2_trans_update(&trans, iter, &insert.k_i);
		ret = bch2_trans_commit(&trans, NULL, NULL, 0);
		BUG_ON(ret);

		if (++i == nr)
			break;
	}
	bch2_trans_exit(&trans);
}

static void seq_lookup(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_DIRENTS, POS_MIN, 0, k, ret)
		;
	bch2_trans_exit(&trans);
}

static void seq_overwrite(struct bch_fs *c, u64 nr)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_DIRENTS, POS_MIN,
			   BTREE_ITER_INTENT, k, ret) {
		struct bkey_i_cookie u;

		bkey_reassemble(&u.k_i, k);

		bch2_trans_update(&trans, iter, &u.k_i);
		ret = bch2_trans_commit(&trans, NULL, NULL, 0);
		BUG_ON(ret);
	}
	bch2_trans_exit(&trans);
}

static void seq_delete(struct bch_fs *c, u64 nr)
{
	int ret;

	ret = bch2_btree_delete_range(c, BTREE_ID_DIRENTS,
				      POS(0, 0), POS(0, U64_MAX),
				      NULL);
	BUG_ON(ret);
}

typedef void (*perf_test_fn)(struct bch_fs *, u64);

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
};

static int btree_perf_test_thread(void *data)
{
	struct test_job *j = data;

	if (atomic_dec_and_test(&j->ready)) {
		wake_up(&j->ready_wait);
		j->start = sched_clock();
	} else {
		wait_event(j->ready_wait, !atomic_read(&j->ready));
	}

	j->fn(j->c, j->nr / j->nr_threads);

	if (atomic_dec_and_test(&j->done)) {
		j->finish = sched_clock();
		complete(&j->done_completion);
	}

	return 0;
}

void bch2_btree_perf_test(struct bch_fs *c, const char *testname,
			  u64 nr, unsigned nr_threads)
{
	struct test_job j = { .c = c, .nr = nr, .nr_threads = nr_threads };
	char name_buf[20], nr_buf[20], per_sec_buf[20];
	unsigned i;
	u64 time;

	atomic_set(&j.ready, nr_threads);
	init_waitqueue_head(&j.ready_wait);

	atomic_set(&j.done, nr_threads);
	init_completion(&j.done_completion);

#define perf_test(_test)				\
	if (!strcmp(testname, #_test)) j.fn = _test

	perf_test(rand_insert);
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

	if (!j.fn) {
		pr_err("unknown test %s", testname);
		return;
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
	bch2_hprint(&PBUF(nr_buf), nr);
	bch2_hprint(&PBUF(per_sec_buf), nr * NSEC_PER_SEC / time);
	printk(KERN_INFO "%-12s %s with %u threads in %5llu sec, %5llu nsec per iter, %5s per sec\n",
		name_buf, nr_buf, nr_threads,
		time / NSEC_PER_SEC,
		time * nr_threads / nr,
		per_sec_buf);
}

#endif /* CONFIG_BCACHEFS_TESTS */
