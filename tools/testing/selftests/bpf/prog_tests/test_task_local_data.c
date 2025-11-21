// SPDX-License-Identifier: GPL-2.0
#include <pthread.h>
#include <bpf/btf.h>
#include <test_progs.h>

#define TLD_FREE_DATA_ON_THREAD_EXIT
#define TLD_DYN_DATA_SIZE 4096
#include "task_local_data.h"

struct test_tld_struct {
	__u64 a;
	__u64 b;
	__u64 c;
	__u64 d;
};

#include "test_task_local_data.skel.h"

TLD_DEFINE_KEY(value0_key, "value0", sizeof(int));

/*
 * Reset task local data between subtests by clearing metadata other
 * than the statically defined value0. This is safe as subtests run
 * sequentially. Users of task local data library should not touch
 * library internal.
 */
static void reset_tld(void)
{
	if (TLD_READ_ONCE(tld_meta_p)) {
		/* Remove TLDs created by tld_create_key() */
		tld_meta_p->cnt = 1;
		tld_meta_p->size = TLD_DYN_DATA_SIZE;
		memset(&tld_meta_p->metadata[1], 0,
		       (TLD_MAX_DATA_CNT - 1) * sizeof(struct tld_metadata));
	}
}

/* Serialize access to bpf program's global variables */
static pthread_mutex_t global_mutex;

static tld_key_t *tld_keys;

#define TEST_BASIC_THREAD_NUM 32

void *test_task_local_data_basic_thread(void *arg)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct test_task_local_data *skel = (struct test_task_local_data *)arg;
	int fd, err, tid, *value0, *value1;
	struct test_tld_struct *value2;

	fd = bpf_map__fd(skel->maps.tld_data_map);

	value0 = tld_get_data(fd, value0_key);
	if (!ASSERT_OK_PTR(value0, "tld_get_data"))
		goto out;

	value1 = tld_get_data(fd, tld_keys[1]);
	if (!ASSERT_OK_PTR(value1, "tld_get_data"))
		goto out;

	value2 = tld_get_data(fd, tld_keys[2]);
	if (!ASSERT_OK_PTR(value2, "tld_get_data"))
		goto out;

	tid = sys_gettid();

	*value0 = tid + 0;
	*value1 = tid + 1;
	value2->a = tid + 2;
	value2->b = tid + 3;
	value2->c = tid + 4;
	value2->d = tid + 5;

	pthread_mutex_lock(&global_mutex);
	/* Run task_main that read task local data and save to global variables */
	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.task_main), &opts);
	ASSERT_OK(err, "run task_main");
	ASSERT_OK(opts.retval, "task_main retval");

	ASSERT_EQ(skel->bss->test_value0, tid + 0, "tld_get_data value0");
	ASSERT_EQ(skel->bss->test_value1, tid + 1, "tld_get_data value1");
	ASSERT_EQ(skel->bss->test_value2.a, tid + 2, "tld_get_data value2.a");
	ASSERT_EQ(skel->bss->test_value2.b, tid + 3, "tld_get_data value2.b");
	ASSERT_EQ(skel->bss->test_value2.c, tid + 4, "tld_get_data value2.c");
	ASSERT_EQ(skel->bss->test_value2.d, tid + 5, "tld_get_data value2.d");
	pthread_mutex_unlock(&global_mutex);

	/* Make sure valueX are indeed local to threads */
	ASSERT_EQ(*value0, tid + 0, "value0");
	ASSERT_EQ(*value1, tid + 1, "value1");
	ASSERT_EQ(value2->a, tid + 2, "value2.a");
	ASSERT_EQ(value2->b, tid + 3, "value2.b");
	ASSERT_EQ(value2->c, tid + 4, "value2.c");
	ASSERT_EQ(value2->d, tid + 5, "value2.d");

	*value0 = tid + 5;
	*value1 = tid + 4;
	value2->a = tid + 3;
	value2->b = tid + 2;
	value2->c = tid + 1;
	value2->d = tid + 0;

	/* Run task_main again */
	pthread_mutex_lock(&global_mutex);
	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.task_main), &opts);
	ASSERT_OK(err, "run task_main");
	ASSERT_OK(opts.retval, "task_main retval");

	ASSERT_EQ(skel->bss->test_value0, tid + 5, "tld_get_data value0");
	ASSERT_EQ(skel->bss->test_value1, tid + 4, "tld_get_data value1");
	ASSERT_EQ(skel->bss->test_value2.a, tid + 3, "tld_get_data value2.a");
	ASSERT_EQ(skel->bss->test_value2.b, tid + 2, "tld_get_data value2.b");
	ASSERT_EQ(skel->bss->test_value2.c, tid + 1, "tld_get_data value2.c");
	ASSERT_EQ(skel->bss->test_value2.d, tid + 0, "tld_get_data value2.d");
	pthread_mutex_unlock(&global_mutex);

out:
	pthread_exit(NULL);
}

static void test_task_local_data_basic(void)
{
	struct test_task_local_data *skel;
	pthread_t thread[TEST_BASIC_THREAD_NUM];
	char dummy_key_name[TLD_NAME_LEN];
	tld_key_t key;
	int i, err;

	reset_tld();

	ASSERT_OK(pthread_mutex_init(&global_mutex, NULL), "pthread_mutex_init");

	skel = test_task_local_data__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	tld_keys = calloc(TLD_MAX_DATA_CNT, sizeof(tld_key_t));
	if (!ASSERT_OK_PTR(tld_keys, "calloc tld_keys"))
		goto out;

	ASSERT_FALSE(tld_key_is_err(value0_key), "TLD_DEFINE_KEY");
	tld_keys[1] = tld_create_key("value1", sizeof(int));
	ASSERT_FALSE(tld_key_is_err(tld_keys[1]), "tld_create_key");
	tld_keys[2] = tld_create_key("value2", sizeof(struct test_tld_struct));
	ASSERT_FALSE(tld_key_is_err(tld_keys[2]), "tld_create_key");

	/*
	 * Shouldn't be able to store data exceed a page. Create a TLD just big
	 * enough to exceed a page. TLDs already created are int value0, int
	 * value1, and struct test_tld_struct value2.
	 */
	key = tld_create_key("value_not_exist",
			     TLD_PAGE_SIZE - 2 * sizeof(int) - sizeof(struct test_tld_struct) + 1);
	ASSERT_EQ(tld_key_err_or_zero(key), -E2BIG, "tld_create_key");

	key = tld_create_key("value2", sizeof(struct test_tld_struct));
	ASSERT_EQ(tld_key_err_or_zero(key), -EEXIST, "tld_create_key");

	/* Shouldn't be able to create the (TLD_MAX_DATA_CNT+1)-th TLD */
	for (i = 3; i < TLD_MAX_DATA_CNT; i++) {
		snprintf(dummy_key_name, TLD_NAME_LEN, "dummy_value%d", i);
		tld_keys[i] = tld_create_key(dummy_key_name, sizeof(int));
		ASSERT_FALSE(tld_key_is_err(tld_keys[i]), "tld_create_key");
	}
	key = tld_create_key("value_not_exist", sizeof(struct test_tld_struct));
	ASSERT_EQ(tld_key_err_or_zero(key), -ENOSPC, "tld_create_key");

	/* Access TLDs from multiple threads and check if they are thread-specific */
	for (i = 0; i < TEST_BASIC_THREAD_NUM; i++) {
		err = pthread_create(&thread[i], NULL, test_task_local_data_basic_thread, skel);
		if (!ASSERT_OK(err, "pthread_create"))
			goto out;
	}

out:
	for (i = 0; i < TEST_BASIC_THREAD_NUM; i++)
		pthread_join(thread[i], NULL);

	if (tld_keys) {
		free(tld_keys);
		tld_keys = NULL;
	}
	tld_free();
	test_task_local_data__destroy(skel);
}

#define TEST_RACE_THREAD_NUM (TLD_MAX_DATA_CNT - 3)

void *test_task_local_data_race_thread(void *arg)
{
	int err = 0, id = (intptr_t)arg;
	char key_name[32];
	tld_key_t key;

	key = tld_create_key("value_not_exist", TLD_PAGE_SIZE + 1);
	if (tld_key_err_or_zero(key) != -E2BIG) {
		err = 1;
		goto out;
	}

	/* Only one thread will succeed in creating value1 */
	key = tld_create_key("value1", sizeof(int));
	if (!tld_key_is_err(key))
		tld_keys[1] = key;

	/* Only one thread will succeed in creating value2 */
	key = tld_create_key("value2", sizeof(struct test_tld_struct));
	if (!tld_key_is_err(key))
		tld_keys[2] = key;

	snprintf(key_name, 32, "thread_%d", id);
	tld_keys[id] = tld_create_key(key_name, sizeof(int));
	if (tld_key_is_err(tld_keys[id]))
		err = 2;
out:
	return (void *)(intptr_t)err;
}

static void test_task_local_data_race(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	pthread_t thread[TEST_RACE_THREAD_NUM];
	struct test_task_local_data *skel;
	int fd, i, j, err, *data;
	void *ret = NULL;

	skel = test_task_local_data__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	tld_keys = calloc(TLD_MAX_DATA_CNT, sizeof(tld_key_t));
	if (!ASSERT_OK_PTR(tld_keys, "calloc tld_keys"))
		goto out;

	fd = bpf_map__fd(skel->maps.tld_data_map);

	ASSERT_FALSE(tld_key_is_err(value0_key), "TLD_DEFINE_KEY");
	tld_keys[0] = value0_key;

	for (j = 0; j < 100; j++) {
		reset_tld();

		for (i = 0; i < TEST_RACE_THREAD_NUM; i++) {
			/*
			 * Try to make tld_create_key() race with each other. Call
			 * tld_create_key(), both valid and invalid, from different threads.
			 */
			err = pthread_create(&thread[i], NULL, test_task_local_data_race_thread,
					     (void *)(intptr_t)(i + 3));
			if (CHECK_FAIL(err))
				break;
		}

		/* Wait for all tld_create_key() to return */
		for (i = 0; i < TEST_RACE_THREAD_NUM; i++) {
			pthread_join(thread[i], &ret);
			if (CHECK_FAIL(ret))
				break;
		}

		/* Write a unique number to each TLD */
		for (i = 0; i < TLD_MAX_DATA_CNT; i++) {
			data = tld_get_data(fd, tld_keys[i]);
			if (CHECK_FAIL(!data))
				break;
			*data = i;
		}

		/* Read TLDs and check the value to see if any address collides with another */
		for (i = 0; i < TLD_MAX_DATA_CNT; i++) {
			data = tld_get_data(fd, tld_keys[i]);
			if (CHECK_FAIL(*data != i))
				break;
		}

		/* Run task_main to make sure no invalid TLDs are added */
		err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.task_main), &opts);
		ASSERT_OK(err, "run task_main");
		ASSERT_OK(opts.retval, "task_main retval");
	}
out:
	if (tld_keys) {
		free(tld_keys);
		tld_keys = NULL;
	}
	tld_free();
	test_task_local_data__destroy(skel);
}

void test_task_local_data(void)
{
	if (test__start_subtest("task_local_data_basic"))
		test_task_local_data_basic();
	if (test__start_subtest("task_local_data_race"))
		test_task_local_data_race();
}
