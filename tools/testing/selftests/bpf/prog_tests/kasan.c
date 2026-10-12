// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/*
 * Tests validating that KASAN reports are properly instrumented and
 * generated on a wide variety of instructions. The running kernel needs
 * kasan_multi_shot to run multiple kasan-generating subtests at once
 */
#include <bpf/bpf.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <unistd.h>
#include <test_progs.h>
#include <unpriv_helpers.h>
#include "sysctl_helpers.h"
#include "kasan.skel.h"
#include "kasan_harden.skel.h"

#define SUBTEST_NAME_MAX_LEN	128
#define PROG_NAME_MAX_LEN	128

#define MAX_LOG_SIZE		(8 * 1024)
#define READ_CHUNK_SIZE		256

#define KASAN_PATTERN_SLAB_UAF "BUG: KASAN: slab-use-after-free " \
	"in bpf_prog_%02x%02x%02x%02x%02x%02x%02x%02x_%s"
#define KASAN_PATTERN_SLAB_OOB "BUG: KASAN: slab-out-of-bounds " \
	"in bpf_prog_%02x%02x%02x%02x%02x%02x%02x%02x_%s"
#define KASAN_PATTERN_REPORT "%s of size %d at addr"

static char klog_buffer[MAX_LOG_SIZE];
static char record[MAX_LOG_SIZE];

struct test_spec {
	char *prog_type;
	bool is_write;
	bool only_32_or_64;
	bool needs_load_acq_store_rel;
	bool needs_st;
	bool skip_multi_size_testing;
	bool skip_on_stack_testing;
	int run_size;
	bool expect_no_report;
	bool rnd_hi32;
	bool is_oob;
};

struct kasan_write_val {
	__u8 data_1;
	__u16 data_2;
	__u32 data_4;
	__u64 data_8;
};

struct test_ctx {
	__u8  prog_tag[BPF_TAG_SIZE];
	struct bpf_object *obj;
	int *access_size;
	bool skip_load_acq_store_rel;
	bool skip_st_tests;
	struct bpf_program *prog;
	char prog_name[SUBTEST_NAME_MAX_LEN];
	int klog_fd;
};

static int open_kernel_logs(void)
{
	int fd;

	fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);

	return fd;
}

static void skip_kernel_logs(int fd)
{
	lseek(fd, 0, SEEK_END);
}

static int read_kernel_logs(int fd, char *buf, size_t max_len)
{
	size_t total = 0;
	ssize_t n;

	buf[0] = '\0';
	while (1) {
		char *msg, *eol;
		size_t len;

		n = read(fd, record, sizeof(record) - 1);
		if (n == 0)
			break;

		if (n < 0) {
			if (errno == EAGAIN)
				break;
			return n;
		}
		record[n] = '\0';

		/*
		 * Each kmsg record starts with some metadata, separated
		 * from the actual content by a semi-colon
		 */
		msg = strchr(record, ';');
		if (!msg)
			continue;
		msg++;
		eol = strchr(msg, '\n');
		if (eol)
			*eol = '\0';

		len = strlen(msg);
		if (total + len + 2 > max_len)
			break;
		memcpy(buf + total, msg, len);
		total += len;
		buf[total++] = '\n';
		buf[total] = '\0';
	}

	return total;
}

static int check_kasan_report_in_kernel_logs(char *buf, struct test_ctx *ctx,
					     bool is_write, int size,
					     bool is_oob)
{
	char access_log[READ_CHUNK_SIZE];
	const char *pattern;
	char *kasan_report_start;
	int nsize;

	pattern = is_oob ? KASAN_PATTERN_SLAB_OOB : KASAN_PATTERN_SLAB_UAF;
	nsize = snprintf(access_log, READ_CHUNK_SIZE, pattern,
			 ctx->prog_tag[0], ctx->prog_tag[1], ctx->prog_tag[2],
			 ctx->prog_tag[3], ctx->prog_tag[4], ctx->prog_tag[5],
			 ctx->prog_tag[6], ctx->prog_tag[7], ctx->prog_name);
	if (!ASSERT_GE(nsize, 0, "format kasan access header line"))
		return nsize;
	/*
	 * Searched kasan report is valid if
	 * - it contains the expected kasan pattern
	 * - the description of the faulty access is found somewhere
	 *   after the header (not necessarily on the very next line,
	 *   because other kernel messages may interleave)
	 * - faulty access properties match the tested type and size
	 */
	kasan_report_start = strstr(buf, access_log);

	if (!kasan_report_start)
		return 1;

	nsize = snprintf(access_log, READ_CHUNK_SIZE, KASAN_PATTERN_REPORT,
			 is_write ? "Write" : "Read", size);
	if (!ASSERT_GE(nsize, 0, "format kasan access report line"))
		return nsize;

	if (!strstr(kasan_report_start, access_log))
		return 1;

	return 0;
}

static void exec_subtest(struct test_ctx *ctx, struct test_spec *test,
			 int access_size, bool on_stack)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct bpf_prog_info info;
	uint8_t buf[ETH_HLEN] = {0};
	int ret, prog_fd;
	__u32 info_len;

	ctx->prog = bpf_object__find_program_by_name(ctx->obj,
						     ctx->prog_name);
	if (!ASSERT_OK_PTR(ctx->prog, "find test prog"))
		return;

	info_len = sizeof(info);
	memset(&info, 0, info_len);
	prog_fd = bpf_program__fd(ctx->prog);
	if (!ASSERT_OK_FD(prog_fd, "get prog fd"))
		return;
	ret = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);
	if (!ASSERT_OK(ret, "fetch loaded program info"))
		return;
	memcpy(ctx->prog_tag, info.tag, BPF_TAG_SIZE);

	skip_kernel_logs(ctx->klog_fd);

	topts.sz = sizeof(struct bpf_test_run_opts);
	topts.data_size_in = ETH_HLEN;
	topts.data_in = buf;
	if (ctx->access_size)
		*ctx->access_size = access_size;
	ret = bpf_prog_test_run_opts(bpf_program__fd(ctx->prog),
				     &topts);
	if (!ASSERT_OK(ret, "run prog"))
		return;

	ret = read_kernel_logs(ctx->klog_fd, klog_buffer, MAX_LOG_SIZE);
	if (!ASSERT_GE(ret, 0, "read kernel logs"))
		return;

	ret = check_kasan_report_in_kernel_logs(klog_buffer, ctx,
						test->is_write, access_size,
						test->is_oob);
	if (on_stack || test->expect_no_report)
		ASSERT_NEQ(ret, 0, "no report should be generated");
	else
		ASSERT_OK(ret, "report should be generated");
}

static void run_subtest_with_size_and_location(struct test_ctx *ctx,
					       struct test_spec *test,
					       int access_size,
					       bool on_stack)
{
	char subtest_name[SUBTEST_NAME_MAX_LEN];

	if (test->skip_multi_size_testing) {
		snprintf(subtest_name, SUBTEST_NAME_MAX_LEN, "%s%s",
			 test->prog_type,
			 test->skip_on_stack_testing ? "" :
			 on_stack		     ? "_on_stack" :
						       "_not_on_stack");
	} else {
		snprintf(subtest_name, SUBTEST_NAME_MAX_LEN, "%s_%d_%s",
			 test->prog_type, access_size,
			 on_stack ? "on_stack" : "not_on_stack");
	}

	snprintf(ctx->prog_name, PROG_NAME_MAX_LEN, "%s%s", test->prog_type,
		 test->skip_on_stack_testing ? "" :
		 on_stack		     ? "_on_stack" :
					       "_not_on_stack");

	if (!test__start_subtest(subtest_name))
		return;

	if (test->needs_load_acq_store_rel && ctx->skip_load_acq_store_rel) {
		test__skip();
		return;
	}

	if (test->needs_st && ctx->skip_st_tests) {
		test__skip();
		return;
	}

	exec_subtest(ctx, test, access_size, on_stack);
}

static void run_subtest_with_size(struct test_ctx *ctx, struct test_spec *test,
				  int size)
{
	run_subtest_with_size_and_location(ctx, test, size, false);
	if (!test->skip_on_stack_testing)
		run_subtest_with_size_and_location(ctx, test, size, true);
}

static void run_subtest(struct test_ctx *ctx, struct test_spec *test)
{
	if (test->skip_multi_size_testing) {
		run_subtest_with_size(ctx, test, test->run_size);
		return;
	}

	if (!test->only_32_or_64) {
		run_subtest_with_size(ctx, test, 1);
		run_subtest_with_size(ctx, test, 2);
	}
	run_subtest_with_size(ctx, test, 4);
	run_subtest_with_size(ctx, test, 8);
}

static void run_blinding_subtest(void)
{
	struct test_spec blinding_spec = {
		.prog_type = "st_blinded",
		.is_write = true,
	};
	char bpf_jit_harden_orig[2];
	struct kasan_harden *skel;
	struct test_ctx *ctx;

	if (!test__start_subtest("st_blinded"))
		return;

	ctx = calloc(1, sizeof(*ctx));
	if (!ASSERT_OK_PTR(ctx, "alloc blinding ctx"))
		return;
	ctx->klog_fd = -1;

	if (sysctl_set_or_fail("/proc/sys/net/core/bpf_jit_harden",
			       bpf_jit_harden_orig, "2"))
		goto free_ctx;

	skel = kasan_harden__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open and load blinded prog"))
		goto restore;

	if (skel->data->skip_st_tests) {
		test__skip();
		goto destroy;
	}

	ctx->klog_fd = open_kernel_logs();
	if (!ASSERT_OK_FD(ctx->klog_fd, "open kernel logs"))
		goto destroy;

	ctx->obj = skel->obj;
	strncpy(ctx->prog_name, "st_blinded", PROG_NAME_MAX_LEN);

	exec_subtest(ctx, &blinding_spec, 1, false);

destroy:
	close(ctx->klog_fd);
	kasan_harden__destroy(skel);
restore:
	sysctl_set_or_fail("/proc/sys/net/core/bpf_jit_harden", NULL,
			   bpf_jit_harden_orig);
free_ctx:
	free(ctx);
}

static struct test_spec tests[] = {
	{
		.prog_type = "st",
		.is_write = true,
		.needs_st = true
	},
	{
		.prog_type = "stx",
		.is_write = true
	},
	{
		.prog_type = "ldx",
		.is_write = false
	},
	{
		.prog_type = "simple_atomic",
		.is_write = true,
		.only_32_or_64 = true
	},
	{
		.prog_type = "simple_atomic_fetch",
		.is_write = true,
		.skip_multi_size_testing = true,
		.run_size = 8,
	},
	{
		.prog_type = "load_acquire",
		.is_write = false,
		.needs_load_acq_store_rel = true
	},
	{
		.prog_type = "store_release",
		.is_write = true,
		.needs_load_acq_store_rel = true
	},
	{
		.prog_type = "ldx_patched",
		.is_write = false,
		.skip_multi_size_testing = true,
		.run_size = 4,
		.rnd_hi32 = true
	},
	{
		.prog_type = "verifier_paths_stack_and_non_stack",
		.is_write = true,
		.skip_multi_size_testing = true,
		.skip_on_stack_testing = true,
		.run_size = 1
	},
	{
		.prog_type = "ldx_oob",
		.is_write = false,
		.skip_on_stack_testing = true,
		.is_oob = true
	},
	{
		.prog_type = "ldx_self_alias_on_stack",
		.is_write = false,
		.skip_multi_size_testing = true,
		.skip_on_stack_testing = true,
		.run_size = 8,
		.expect_no_report = true
	}
};

void serial_test_kasan(void)
{
	struct kasan_write_val val;
	struct test_spec *test;
	struct test_ctx *ctx;
	struct kasan *skel;
	__u32 key = 0;
	int i, ret;

	ctx = calloc(1, sizeof(struct test_ctx));
	if (!ASSERT_OK_PTR(ctx, "alloc test ctx"))
		return;

	if (!is_jit_enabled() || !get_kasan_jit_enabled() ||
	    !get_kasan_multi_shot_enabled()) {
		test__skip();
		goto end;
	}

	skel = kasan__open();
	if (!ASSERT_OK_PTR(skel, "open prog"))
		goto end;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		char prog_name[SUBTEST_NAME_MAX_LEN];
		struct bpf_program *prog;

		if (!tests[i].rnd_hi32)
			continue;

		snprintf(prog_name, SUBTEST_NAME_MAX_LEN, "%s_%s",
			 tests[i].prog_type, "on_stack");
		prog = bpf_object__find_program_by_name(skel->obj, prog_name);
		if (!ASSERT_OK_PTR(prog, "find rnd_hi32 on_stack prog"))
			goto destroy;
		bpf_program__set_flags(prog, BPF_F_TEST_RND_HI32);
		snprintf(prog_name, SUBTEST_NAME_MAX_LEN, "%s_%s",
			 tests[i].prog_type, "not_on_stack");
		prog = bpf_object__find_program_by_name(skel->obj, prog_name);
		if (!ASSERT_OK_PTR(prog, "find rnd_hi32 not_on_stack prog"))
			goto destroy;
		bpf_program__set_flags(prog, BPF_F_TEST_RND_HI32);
	}

	if (!ASSERT_OK(kasan__load(skel), "load prog"))
		goto destroy;

	ctx->obj = skel->obj;
	ctx->access_size = &skel->bss->access_size;
	ctx->skip_load_acq_store_rel = skel->data->skip_load_acq_store_rel_tests;
	ctx->skip_st_tests = skel->data->skip_st_tests;

	ctx->klog_fd = open_kernel_logs();
	if (!ASSERT_OK_FD(ctx->klog_fd, "open kernel logs"))
		goto destroy;

	/* Fill map with recognizable values */
	ret = bpf_map__lookup_elem(skel->maps.test_map, &key, sizeof(key),
				   &val, sizeof(val), 0);
	if (!ASSERT_OK(ret, "get map"))
		goto close;
	val.data_1 = 0xAA;
	val.data_2 = 0xBBBB;
	val.data_4 = 0xCCCCCCCC;
	val.data_8 = 0xDDDDDDDDDDDDDDDD;
	ret = bpf_map__update_elem(skel->maps.test_map, &key, sizeof(key),
				   &val, sizeof(val), 0);
	if (!ASSERT_OK(ret, "set map"))
		goto close;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		test = &tests[i];
		run_subtest(ctx, test);
	}

	/*
	 * Blinding subtest is handled differently as it needs the
	 * corresponding program to be loaded with bpf_jit_harden raised
	 */
	run_blinding_subtest();

close:
	close(ctx->klog_fd);
destroy:
	kasan__destroy(skel);
end:
	free(ctx);
}
