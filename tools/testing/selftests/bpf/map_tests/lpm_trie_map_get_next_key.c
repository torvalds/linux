// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <linux/bpf.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <test_maps.h>

struct test_lpm_key {
	__u32 prefix;
	__u32 data;
};

struct get_next_key_ctx {
	struct test_lpm_key key;
	bool start;
	bool stop;
	int map_fd;
	int loop;
};

static void *get_next_key_fn(void *arg)
{
	struct get_next_key_ctx *ctx = arg;
	struct test_lpm_key next_key;
	int i = 0;

	while (!ctx->start)
		usleep(1);

	while (!ctx->stop && i++ < ctx->loop)
		bpf_map_get_next_key(ctx->map_fd, &ctx->key, &next_key);

	return NULL;
}

static void abort_get_next_key(struct get_next_key_ctx *ctx, pthread_t *tids,
			       unsigned int nr)
{
	unsigned int i;

	ctx->stop = true;
	ctx->start = true;
	for (i = 0; i < nr; i++)
		pthread_join(tids[i], NULL);
}

/* This test aims to prevent regression of future. As long as the kernel does
 * not panic, it is considered as success.
 */
void test_lpm_trie_map_get_next_key(void)
{
#define MAX_NR_THREADS 8
	LIBBPF_OPTS(bpf_map_create_opts, create_opts,
		    .map_flags = BPF_F_NO_PREALLOC);
	struct test_lpm_key key = {};
	__u32 val = 0;
	int map_fd;
	const __u32 max_prefixlen = 8 * (sizeof(key) - sizeof(key.prefix));
	const __u32 max_entries = max_prefixlen + 1;
	unsigned int i, nr = MAX_NR_THREADS, loop = 65536;
	pthread_t tids[MAX_NR_THREADS];
	struct get_next_key_ctx ctx;
	int err;

	map_fd = bpf_map_create(BPF_MAP_TYPE_LPM_TRIE, "lpm_trie_map",
				sizeof(struct test_lpm_key), sizeof(__u32),
				max_entries, &create_opts);
	CHECK(map_fd == -1, "bpf_map_create()", "error:%s\n",
	      strerror(errno));

	for (i = 0; i <= max_prefixlen; i++) {
		key.prefix = i;
		err = bpf_map_update_elem(map_fd, &key, &val, BPF_ANY);
		CHECK(err, "bpf_map_update_elem()", "error:%s\n",
		      strerror(errno));
	}

	ctx.start = false;
	ctx.stop = false;
	ctx.map_fd = map_fd;
	ctx.loop = loop;
	memcpy(&ctx.key, &key, sizeof(key));

	for (i = 0; i < nr; i++) {
		err = pthread_create(&tids[i], NULL, get_next_key_fn, &ctx);
		if (err) {
			abort_get_next_key(&ctx, tids, i);
			CHECK(err, "pthread_create", "error %d\n", err);
		}
	}

	ctx.start = true;
	for (i = 0; i < nr; i++)
		pthread_join(tids[i], NULL);

	printf("%s:PASS\n", __func__);

	close(map_fd);
}
