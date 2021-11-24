// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook  */
#include <linux/compiler.h>
#include <linux/err.h>

#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/btf.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <test_btf.h>
#include <test_maps.h>

static struct bpf_map_create_opts map_opts = {
	.sz = sizeof(map_opts),
	.btf_key_type_id = 1,
	.btf_value_type_id = 3,
	.btf_fd = -1,
	.map_flags = BPF_F_NO_PREALLOC,
};

static unsigned int nr_sk_threads_done;
static unsigned int nr_sk_threads_err;
static unsigned int nr_sk_per_thread = 4096;
static unsigned int nr_sk_threads = 4;
static int sk_storage_map = -1;
static unsigned int stop;
static int runtime_s = 5;

static bool is_stopped(void)
{
	return READ_ONCE(stop);
}

static unsigned int threads_err(void)
{
	return READ_ONCE(nr_sk_threads_err);
}

static void notify_thread_err(void)
{
	__sync_add_and_fetch(&nr_sk_threads_err, 1);
}

static bool wait_for_threads_err(void)
{
	while (!is_stopped() && !threads_err())
		usleep(500);

	return !is_stopped();
}

static unsigned int threads_done(void)
{
	return READ_ONCE(nr_sk_threads_done);
}

static void notify_thread_done(void)
{
	__sync_add_and_fetch(&nr_sk_threads_done, 1);
}

static void notify_thread_redo(void)
{
	__sync_sub_and_fetch(&nr_sk_threads_done, 1);
}

static bool wait_for_threads_done(void)
{
	while (threads_done() != nr_sk_threads && !is_stopped() &&
	       !threads_err())
		usleep(50);

	return !is_stopped() && !threads_err();
}

static bool wait_for_threads_redo(void)
{
	while (threads_done() && !is_stopped() && !threads_err())
		usleep(50);

	return !is_stopped() && !threads_err();
}

static bool wait_for_map(void)
{
	while (READ_ONCE(sk_storage_map) == -1 && !is_stopped())
		usleep(50);

	return !is_stopped();
}

static bool wait_for_map_close(void)
{
	while (READ_ONCE(sk_storage_map) != -1 && !is_stopped())
		;

	return !is_stopped();
}

static int load_btf(void)
{
	const char btf_str_sec[] = "\0bpf_spin_lock\0val\0cnt\0l";
	__u32 btf_raw_types[] = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* struct bpf_spin_lock */                      /* [2] */
		BTF_TYPE_ENC(1, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 4),
		BTF_MEMBER_ENC(15, 1, 0), /* int val; */
		/* struct val */                                /* [3] */
		BTF_TYPE_ENC(15, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), 8),
		BTF_MEMBER_ENC(19, 1, 0), /* int cnt; */
		BTF_MEMBER_ENC(23, 2, 32),/* struct bpf_spin_lock l; */
	};
	struct btf_header btf_hdr = {
		.magic = BTF_MAGIC,
		.version = BTF_VERSION,
		.hdr_len = sizeof(struct btf_header),
		.type_len = sizeof(btf_raw_types),
		.str_off = sizeof(btf_raw_types),
		.str_len = sizeof(btf_str_sec),
	};
	__u8 raw_btf[sizeof(struct btf_header) + sizeof(btf_raw_types) +
		     sizeof(btf_str_sec)];

	memcpy(raw_btf, &btf_hdr, sizeof(btf_hdr));
	memcpy(raw_btf + sizeof(btf_hdr), btf_raw_types, sizeof(btf_raw_types));
	memcpy(raw_btf + sizeof(btf_hdr) + sizeof(btf_raw_types),
	       btf_str_sec, sizeof(btf_str_sec));

	return bpf_load_btf(raw_btf, sizeof(raw_btf), 0, 0, 0);
}

static int create_sk_storage_map(void)
{
	int btf_fd, map_fd;

	btf_fd = load_btf();
	CHECK(btf_fd == -1, "bpf_load_btf", "btf_fd:%d errno:%d\n",
	      btf_fd, errno);
	map_opts.btf_fd = btf_fd;

	map_fd = bpf_map_create(BPF_MAP_TYPE_SK_STORAGE, "sk_storage_map", 4, 8, 0, &map_opts);
	map_opts.btf_fd = -1;
	close(btf_fd);
	CHECK(map_fd == -1,
	      "bpf_map_create()", "errno:%d\n", errno);

	return map_fd;
}

static void *insert_close_thread(void *arg)
{
	struct {
		int cnt;
		int lock;
	} value = { .cnt = 0xeB9F, .lock = 0, };
	int i, map_fd, err, *sk_fds;

	sk_fds = malloc(sizeof(*sk_fds) * nr_sk_per_thread);
	if (!sk_fds) {
		notify_thread_err();
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < nr_sk_per_thread; i++)
		sk_fds[i] = -1;

	while (!is_stopped()) {
		if (!wait_for_map())
			goto close_all;

		map_fd = READ_ONCE(sk_storage_map);
		for (i = 0; i < nr_sk_per_thread && !is_stopped(); i++) {
			sk_fds[i] = socket(AF_INET6, SOCK_STREAM, 0);
			if (sk_fds[i] == -1) {
				err = -errno;
				fprintf(stderr, "socket(): errno:%d\n", errno);
				goto errout;
			}
			err = bpf_map_update_elem(map_fd, &sk_fds[i], &value,
						  BPF_NOEXIST);
			if (err) {
				err = -errno;
				fprintf(stderr,
					"bpf_map_update_elem(): errno:%d\n",
					errno);
				goto errout;
			}
		}

		notify_thread_done();
		wait_for_map_close();

close_all:
		for (i = 0; i < nr_sk_per_thread; i++) {
			close(sk_fds[i]);
			sk_fds[i] = -1;
		}

		notify_thread_redo();
	}

	free(sk_fds);
	return NULL;

errout:
	for (i = 0; i < nr_sk_per_thread && sk_fds[i] != -1; i++)
		close(sk_fds[i]);
	free(sk_fds);
	notify_thread_err();
	return ERR_PTR(err);
}

static int do_sk_storage_map_stress_free(void)
{
	int i, map_fd = -1, err = 0, nr_threads_created = 0;
	pthread_t *sk_thread_ids;
	void *thread_ret;

	sk_thread_ids = malloc(sizeof(pthread_t) * nr_sk_threads);
	if (!sk_thread_ids) {
		fprintf(stderr, "malloc(sk_threads): NULL\n");
		return -ENOMEM;
	}

	for (i = 0; i < nr_sk_threads; i++) {
		err = pthread_create(&sk_thread_ids[i], NULL,
				     insert_close_thread, NULL);
		if (err) {
			err = -errno;
			goto done;
		}
		nr_threads_created++;
	}

	while (!is_stopped()) {
		map_fd = create_sk_storage_map();
		WRITE_ONCE(sk_storage_map, map_fd);

		if (!wait_for_threads_done())
			break;

		WRITE_ONCE(sk_storage_map, -1);
		close(map_fd);
		map_fd = -1;

		if (!wait_for_threads_redo())
			break;
	}

done:
	WRITE_ONCE(stop, 1);
	for (i = 0; i < nr_threads_created; i++) {
		pthread_join(sk_thread_ids[i], &thread_ret);
		if (IS_ERR(thread_ret) && !err) {
			err = PTR_ERR(thread_ret);
			fprintf(stderr, "threads#%u: err:%d\n", i, err);
		}
	}
	free(sk_thread_ids);

	if (map_fd != -1)
		close(map_fd);

	return err;
}

static void *update_thread(void *arg)
{
	struct {
		int cnt;
		int lock;
	} value = { .cnt = 0xeB9F, .lock = 0, };
	int map_fd = READ_ONCE(sk_storage_map);
	int sk_fd = *(int *)arg;
	int err = 0; /* Suppress compiler false alarm */

	while (!is_stopped()) {
		err = bpf_map_update_elem(map_fd, &sk_fd, &value, 0);
		if (err && errno != EAGAIN) {
			err = -errno;
			fprintf(stderr, "bpf_map_update_elem: %d %d\n",
				err, errno);
			break;
		}
	}

	if (!is_stopped()) {
		notify_thread_err();
		return ERR_PTR(err);
	}

	return NULL;
}

static void *delete_thread(void *arg)
{
	int map_fd = READ_ONCE(sk_storage_map);
	int sk_fd = *(int *)arg;
	int err = 0; /* Suppress compiler false alarm */

	while (!is_stopped()) {
		err = bpf_map_delete_elem(map_fd, &sk_fd);
		if (err && errno != ENOENT) {
			err = -errno;
			fprintf(stderr, "bpf_map_delete_elem: %d %d\n",
				err, errno);
			break;
		}
	}

	if (!is_stopped()) {
		notify_thread_err();
		return ERR_PTR(err);
	}

	return NULL;
}

static int do_sk_storage_map_stress_change(void)
{
	int i, sk_fd, map_fd = -1, err = 0, nr_threads_created = 0;
	pthread_t *sk_thread_ids;
	void *thread_ret;

	sk_thread_ids = malloc(sizeof(pthread_t) * nr_sk_threads);
	if (!sk_thread_ids) {
		fprintf(stderr, "malloc(sk_threads): NULL\n");
		return -ENOMEM;
	}

	sk_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (sk_fd == -1) {
		err = -errno;
		goto done;
	}

	map_fd = create_sk_storage_map();
	WRITE_ONCE(sk_storage_map, map_fd);

	for (i = 0; i < nr_sk_threads; i++) {
		if (i & 0x1)
			err = pthread_create(&sk_thread_ids[i], NULL,
					     update_thread, &sk_fd);
		else
			err = pthread_create(&sk_thread_ids[i], NULL,
					     delete_thread, &sk_fd);
		if (err) {
			err = -errno;
			goto done;
		}
		nr_threads_created++;
	}

	wait_for_threads_err();

done:
	WRITE_ONCE(stop, 1);
	for (i = 0; i < nr_threads_created; i++) {
		pthread_join(sk_thread_ids[i], &thread_ret);
		if (IS_ERR(thread_ret) && !err) {
			err = PTR_ERR(thread_ret);
			fprintf(stderr, "threads#%u: err:%d\n", i, err);
		}
	}
	free(sk_thread_ids);

	if (sk_fd != -1)
		close(sk_fd);
	close(map_fd);

	return err;
}

static void stop_handler(int signum)
{
	if (signum != SIGALRM)
		printf("stopping...\n");
	WRITE_ONCE(stop, 1);
}

#define BPF_SK_STORAGE_MAP_TEST_NR_THREADS "BPF_SK_STORAGE_MAP_TEST_NR_THREADS"
#define BPF_SK_STORAGE_MAP_TEST_SK_PER_THREAD "BPF_SK_STORAGE_MAP_TEST_SK_PER_THREAD"
#define BPF_SK_STORAGE_MAP_TEST_RUNTIME_S "BPF_SK_STORAGE_MAP_TEST_RUNTIME_S"
#define BPF_SK_STORAGE_MAP_TEST_NAME "BPF_SK_STORAGE_MAP_TEST_NAME"

static void test_sk_storage_map_stress_free(void)
{
	struct rlimit rlim_old, rlim_new = {};
	int err;

	getrlimit(RLIMIT_NOFILE, &rlim_old);

	signal(SIGTERM, stop_handler);
	signal(SIGINT, stop_handler);
	if (runtime_s > 0) {
		signal(SIGALRM, stop_handler);
		alarm(runtime_s);
	}

	if (rlim_old.rlim_cur < nr_sk_threads * nr_sk_per_thread) {
		rlim_new.rlim_cur = nr_sk_threads * nr_sk_per_thread + 128;
		rlim_new.rlim_max = rlim_new.rlim_cur + 128;
		err = setrlimit(RLIMIT_NOFILE, &rlim_new);
		CHECK(err, "setrlimit(RLIMIT_NOFILE)", "rlim_new:%lu errno:%d",
		      rlim_new.rlim_cur, errno);
	}

	err = do_sk_storage_map_stress_free();

	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	if (runtime_s > 0) {
		signal(SIGALRM, SIG_DFL);
		alarm(0);
	}

	if (rlim_new.rlim_cur)
		setrlimit(RLIMIT_NOFILE, &rlim_old);

	CHECK(err, "test_sk_storage_map_stress_free", "err:%d\n", err);
}

static void test_sk_storage_map_stress_change(void)
{
	int err;

	signal(SIGTERM, stop_handler);
	signal(SIGINT, stop_handler);
	if (runtime_s > 0) {
		signal(SIGALRM, stop_handler);
		alarm(runtime_s);
	}

	err = do_sk_storage_map_stress_change();

	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	if (runtime_s > 0) {
		signal(SIGALRM, SIG_DFL);
		alarm(0);
	}

	CHECK(err, "test_sk_storage_map_stress_change", "err:%d\n", err);
}

static void test_sk_storage_map_basic(void)
{
	struct {
		int cnt;
		int lock;
	} value = { .cnt = 0xeB9f, .lock = 0, }, lookup_value;
	struct bpf_map_create_opts bad_xattr;
	int btf_fd, map_fd, sk_fd, err;

	btf_fd = load_btf();
	CHECK(btf_fd == -1, "bpf_load_btf", "btf_fd:%d errno:%d\n",
	      btf_fd, errno);
	map_opts.btf_fd = btf_fd;

	sk_fd = socket(AF_INET6, SOCK_STREAM, 0);
	CHECK(sk_fd == -1, "socket()", "sk_fd:%d errno:%d\n",
	      sk_fd, errno);

	map_fd = bpf_map_create(BPF_MAP_TYPE_SK_STORAGE, "sk_storage_map", 4, 8, 0, &map_opts);
	CHECK(map_fd == -1, "bpf_map_create(good_xattr)",
	      "map_fd:%d errno:%d\n", map_fd, errno);

	/* Add new elem */
	memcpy(&lookup_value, &value, sizeof(value));
	err = bpf_map_update_elem(map_fd, &sk_fd, &value,
				  BPF_NOEXIST | BPF_F_LOCK);
	CHECK(err, "bpf_map_update_elem(BPF_NOEXIST|BPF_F_LOCK)",
	      "err:%d errno:%d\n", err, errno);
	err = bpf_map_lookup_elem_flags(map_fd, &sk_fd, &lookup_value,
					BPF_F_LOCK);
	CHECK(err || lookup_value.cnt != value.cnt,
	      "bpf_map_lookup_elem_flags(BPF_F_LOCK)",
	      "err:%d errno:%d cnt:%x(%x)\n",
	      err, errno, lookup_value.cnt, value.cnt);

	/* Bump the cnt and update with BPF_EXIST | BPF_F_LOCK */
	value.cnt += 1;
	err = bpf_map_update_elem(map_fd, &sk_fd, &value,
				  BPF_EXIST | BPF_F_LOCK);
	CHECK(err, "bpf_map_update_elem(BPF_EXIST|BPF_F_LOCK)",
	      "err:%d errno:%d\n", err, errno);
	err = bpf_map_lookup_elem_flags(map_fd, &sk_fd, &lookup_value,
					BPF_F_LOCK);
	CHECK(err || lookup_value.cnt != value.cnt,
	      "bpf_map_lookup_elem_flags(BPF_F_LOCK)",
	      "err:%d errno:%d cnt:%x(%x)\n",
	      err, errno, lookup_value.cnt, value.cnt);

	/* Bump the cnt and update with BPF_EXIST */
	value.cnt += 1;
	err = bpf_map_update_elem(map_fd, &sk_fd, &value, BPF_EXIST);
	CHECK(err, "bpf_map_update_elem(BPF_EXIST)",
	      "err:%d errno:%d\n", err, errno);
	err = bpf_map_lookup_elem_flags(map_fd, &sk_fd, &lookup_value,
					BPF_F_LOCK);
	CHECK(err || lookup_value.cnt != value.cnt,
	      "bpf_map_lookup_elem_flags(BPF_F_LOCK)",
	      "err:%d errno:%d cnt:%x(%x)\n",
	      err, errno, lookup_value.cnt, value.cnt);

	/* Update with BPF_NOEXIST */
	value.cnt += 1;
	err = bpf_map_update_elem(map_fd, &sk_fd, &value,
				  BPF_NOEXIST | BPF_F_LOCK);
	CHECK(!err || errno != EEXIST,
	      "bpf_map_update_elem(BPF_NOEXIST|BPF_F_LOCK)",
	      "err:%d errno:%d\n", err, errno);
	err = bpf_map_update_elem(map_fd, &sk_fd, &value, BPF_NOEXIST);
	CHECK(!err || errno != EEXIST, "bpf_map_update_elem(BPF_NOEXIST)",
	      "err:%d errno:%d\n", err, errno);
	value.cnt -= 1;
	err = bpf_map_lookup_elem_flags(map_fd, &sk_fd, &lookup_value,
					BPF_F_LOCK);
	CHECK(err || lookup_value.cnt != value.cnt,
	      "bpf_map_lookup_elem_flags(BPF_F_LOCK)",
	      "err:%d errno:%d cnt:%x(%x)\n",
	      err, errno, lookup_value.cnt, value.cnt);

	/* Bump the cnt again and update with map_flags == 0 */
	value.cnt += 1;
	err = bpf_map_update_elem(map_fd, &sk_fd, &value, 0);
	CHECK(err, "bpf_map_update_elem()", "err:%d errno:%d\n",
	      err, errno);
	err = bpf_map_lookup_elem_flags(map_fd, &sk_fd, &lookup_value,
					BPF_F_LOCK);
	CHECK(err || lookup_value.cnt != value.cnt,
	      "bpf_map_lookup_elem_flags(BPF_F_LOCK)",
	      "err:%d errno:%d cnt:%x(%x)\n",
	      err, errno, lookup_value.cnt, value.cnt);

	/* Test delete elem */
	err = bpf_map_delete_elem(map_fd, &sk_fd);
	CHECK(err, "bpf_map_delete_elem()", "err:%d errno:%d\n",
	      err, errno);
	err = bpf_map_lookup_elem_flags(map_fd, &sk_fd, &lookup_value,
					BPF_F_LOCK);
	CHECK(!err || errno != ENOENT,
	      "bpf_map_lookup_elem_flags(BPF_F_LOCK)",
	      "err:%d errno:%d\n", err, errno);
	err = bpf_map_delete_elem(map_fd, &sk_fd);
	CHECK(!err || errno != ENOENT, "bpf_map_delete_elem()",
	      "err:%d errno:%d\n", err, errno);

	memcpy(&bad_xattr, &map_opts, sizeof(map_opts));
	bad_xattr.btf_key_type_id = 0;
	err = bpf_map_create(BPF_MAP_TYPE_SK_STORAGE, "sk_storage_map", 4, 8, 0, &bad_xattr);
	CHECK(!err || errno != EINVAL, "bpf_map_create(bad_xattr)",
	      "err:%d errno:%d\n", err, errno);

	memcpy(&bad_xattr, &map_opts, sizeof(map_opts));
	bad_xattr.btf_key_type_id = 3;
	err = bpf_map_create(BPF_MAP_TYPE_SK_STORAGE, "sk_storage_map", 4, 8, 0, &bad_xattr);
	CHECK(!err || errno != EINVAL, "bpf_map_create(bad_xattr)",
	      "err:%d errno:%d\n", err, errno);

	err = bpf_map_create(BPF_MAP_TYPE_SK_STORAGE, "sk_storage_map", 4, 8, 1, &map_opts);
	CHECK(!err || errno != EINVAL, "bpf_map_create(bad_xattr)",
	      "err:%d errno:%d\n", err, errno);

	memcpy(&bad_xattr, &map_opts, sizeof(map_opts));
	bad_xattr.map_flags = 0;
	err = bpf_map_create(BPF_MAP_TYPE_SK_STORAGE, "sk_storage_map", 4, 8, 0, &bad_xattr);
	CHECK(!err || errno != EINVAL, "bap_create_map_xattr(bad_xattr)",
	      "err:%d errno:%d\n", err, errno);

	map_opts.btf_fd = -1;
	close(btf_fd);
	close(map_fd);
	close(sk_fd);
}

void test_sk_storage_map(void)
{
	const char *test_name, *env_opt;
	bool test_ran = false;

	test_name = getenv(BPF_SK_STORAGE_MAP_TEST_NAME);

	env_opt = getenv(BPF_SK_STORAGE_MAP_TEST_NR_THREADS);
	if (env_opt)
		nr_sk_threads = atoi(env_opt);

	env_opt = getenv(BPF_SK_STORAGE_MAP_TEST_SK_PER_THREAD);
	if (env_opt)
		nr_sk_per_thread = atoi(env_opt);

	env_opt = getenv(BPF_SK_STORAGE_MAP_TEST_RUNTIME_S);
	if (env_opt)
		runtime_s = atoi(env_opt);

	if (!test_name || !strcmp(test_name, "basic")) {
		test_sk_storage_map_basic();
		test_ran = true;
	}
	if (!test_name || !strcmp(test_name, "stress_free")) {
		test_sk_storage_map_stress_free();
		test_ran = true;
	}
	if (!test_name || !strcmp(test_name, "stress_change")) {
		test_sk_storage_map_stress_change();
		test_ran = true;
	}

	if (test_ran)
		printf("%s:PASS\n", __func__);
	else
		CHECK(1, "Invalid test_name", "%s\n", test_name);
}
