// SPDX-License-Identifier: GPL-2.0
/* Copyright 2026 Google LLC */

#include <test_progs.h>
#include <bpf/btf.h>
#include <fcntl.h>
#include "test_wakeup_source.skel.h"
#include "wakeup_source_fail.skel.h"
#include "progs/wakeup_source.h"

static int lock_ws(const char *name)
{
	int fd;
	ssize_t bytes;

	fd = open("/sys/power/wake_lock", O_WRONLY);
	if (!ASSERT_OK_FD(fd, "open /sys/power/wake_lock"))
		return -1;

	bytes = write(fd, name, strlen(name));
	close(fd);
	if (!ASSERT_EQ(bytes, strlen(name), "write to wake_lock"))
		return -1;

	return 0;
}

static void unlock_ws(const char *name)
{
	int fd;

	fd = open("/sys/power/wake_unlock", O_WRONLY);
	if (fd < 0)
		return;

	write(fd, name, strlen(name));
	close(fd);
}

struct rb_ctx {
	const char *name;
	bool found;
	long long active_time_ns;
	long long total_time_ns;
};

static int process_sample(void *ctx, void *data, size_t len)
{
	struct rb_ctx *rb_ctx = ctx;
	struct wakeup_event_t *e = data;

	if (strcmp(e->name, rb_ctx->name) == 0) {
		rb_ctx->found = true;
		rb_ctx->active_time_ns = e->active_time_ns;
		rb_ctx->total_time_ns = e->total_time_ns;
	}
	return 0;
}

void test_wakeup_source(void)
{
	struct btf *btf;
	int id;

	btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(btf, "btf_vmlinux"))
		return;

	id = btf__find_by_name_kind(btf, "bpf_wakeup_sources_get_head", BTF_KIND_FUNC);
	btf__free(btf);

	if (id < 0) {
		printf("%s:SKIP:bpf_wakeup_sources_get_head kfunc not found in BTF\n", __func__);
		test__skip();
		return;
	}

	if (test__start_subtest("iterate_and_verify_times")) {
		struct test_wakeup_source *skel;
		struct ring_buffer *rb = NULL;
		struct rb_ctx rb_ctx = {
			.name = "bpf_selftest_ws_times",
			.found = false,
		};
		int err;

		skel = test_wakeup_source__open_and_load();
		if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
			return;

		rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), process_sample, &rb_ctx, NULL);
		if (!ASSERT_OK_PTR(rb, "ring_buffer__new"))
			goto destroy;

		/* Create a temporary wakeup source */
		if (!ASSERT_OK(lock_ws(rb_ctx.name), "lock_ws"))
			goto unlock;

		err = bpf_prog_test_run_opts(bpf_program__fd(
				skel->progs.iterate_wakeupsources), NULL);
		ASSERT_OK(err, "bpf_prog_test_run");

		ring_buffer__consume(rb);

		ASSERT_TRUE(rb_ctx.found, "found_test_ws_in_rb");
		ASSERT_GT(rb_ctx.active_time_ns, 0, "active_time_gt_0");
		ASSERT_GT(rb_ctx.total_time_ns, 0, "total_time_gt_0");

unlock:
		unlock_ws(rb_ctx.name);
destroy:
		if (rb)
			ring_buffer__free(rb);
		test_wakeup_source__destroy(skel);
	}

	RUN_TESTS(wakeup_source_fail);
}
