// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <util/record.h>
#include <util/util.h>
#include <util/bpf-loader.h>
#include <util/evlist.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <api/fs/fs.h>
#include <perf/mmap.h>
#include "tests.h"
#include "llvm.h"
#include "debug.h"
#include "parse-events.h"
#include "util/mmap.h"
#define NR_ITERS       111
#define PERF_TEST_BPF_PATH "/sys/fs/bpf/perf_test"

#ifdef HAVE_LIBBPF_SUPPORT
#include <linux/bpf.h>
#include <bpf/bpf.h>

static int epoll_pwait_loop(void)
{
	int i;

	/* Should fail NR_ITERS times */
	for (i = 0; i < NR_ITERS; i++)
		epoll_pwait(-(i + 1), NULL, 0, 0, NULL);
	return 0;
}

#ifdef HAVE_BPF_PROLOGUE

static int llseek_loop(void)
{
	int fds[2], i;

	fds[0] = open("/dev/null", O_RDONLY);
	fds[1] = open("/dev/null", O_RDWR);

	if (fds[0] < 0 || fds[1] < 0)
		return -1;

	for (i = 0; i < NR_ITERS; i++) {
		lseek(fds[i % 2], i, (i / 2) % 2 ? SEEK_CUR : SEEK_SET);
		lseek(fds[(i + 1) % 2], i, (i / 2) % 2 ? SEEK_CUR : SEEK_SET);
	}
	close(fds[0]);
	close(fds[1]);
	return 0;
}

#endif

static struct {
	enum test_llvm__testcase prog_id;
	const char *name;
	const char *msg_compile_fail;
	const char *msg_load_fail;
	int (*target_func)(void);
	int expect_result;
	bool	pin;
} bpf_testcase_table[] = {
	{
		.prog_id	  = LLVM_TESTCASE_BASE,
		.name		  = "[basic_bpf_test]",
		.msg_compile_fail = "fix 'perf test LLVM' first",
		.msg_load_fail	  = "load bpf object failed",
		.target_func	  = &epoll_pwait_loop,
		.expect_result	  = (NR_ITERS + 1) / 2,
	},
	{
		.prog_id	  = LLVM_TESTCASE_BASE,
		.name		  = "[bpf_pinning]",
		.msg_compile_fail = "fix kbuild first",
		.msg_load_fail	  = "check your vmlinux setting?",
		.target_func	  = &epoll_pwait_loop,
		.expect_result	  = (NR_ITERS + 1) / 2,
		.pin		  = true,
	},
#ifdef HAVE_BPF_PROLOGUE
	{
		.prog_id	  = LLVM_TESTCASE_BPF_PROLOGUE,
		.name		  = "[bpf_prologue_test]",
		.msg_compile_fail = "fix kbuild first",
		.msg_load_fail	  = "check your vmlinux setting?",
		.target_func	  = &llseek_loop,
		.expect_result	  = (NR_ITERS + 1) / 4,
	},
#endif
};

static int do_test(struct bpf_object *obj, int (*func)(void),
		   int expect)
{
	struct record_opts opts = {
		.target = {
			.uid = UINT_MAX,
			.uses_mmap = true,
		},
		.freq	      = 0,
		.mmap_pages   = 256,
		.default_interval = 1,
	};

	char pid[16];
	char sbuf[STRERR_BUFSIZE];
	struct evlist *evlist;
	int i, ret = TEST_FAIL, err = 0, count = 0;

	struct parse_events_state parse_state;
	struct parse_events_error parse_error;

	parse_events_error__init(&parse_error);
	bzero(&parse_state, sizeof(parse_state));
	parse_state.error = &parse_error;
	INIT_LIST_HEAD(&parse_state.list);

	err = parse_events_load_bpf_obj(&parse_state, &parse_state.list, obj, NULL);
	parse_events_error__exit(&parse_error);
	if (err || list_empty(&parse_state.list)) {
		pr_debug("Failed to add events selected by BPF\n");
		return TEST_FAIL;
	}

	snprintf(pid, sizeof(pid), "%d", getpid());
	pid[sizeof(pid) - 1] = '\0';
	opts.target.tid = opts.target.pid = pid;

	/* Instead of evlist__new_default, don't add default events */
	evlist = evlist__new();
	if (!evlist) {
		pr_debug("Not enough memory to create evlist\n");
		return TEST_FAIL;
	}

	err = evlist__create_maps(evlist, &opts.target);
	if (err < 0) {
		pr_debug("Not enough memory to create thread/cpu maps\n");
		goto out_delete_evlist;
	}

	evlist__splice_list_tail(evlist, &parse_state.list);
	evlist->core.nr_groups = parse_state.nr_groups;

	evlist__config(evlist, &opts, NULL);

	err = evlist__open(evlist);
	if (err < 0) {
		pr_debug("perf_evlist__open: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	err = evlist__mmap(evlist, opts.mmap_pages);
	if (err < 0) {
		pr_debug("evlist__mmap: %s\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	evlist__enable(evlist);
	(*func)();
	evlist__disable(evlist);

	for (i = 0; i < evlist->core.nr_mmaps; i++) {
		union perf_event *event;
		struct mmap *md;

		md = &evlist->mmap[i];
		if (perf_mmap__read_init(&md->core) < 0)
			continue;

		while ((event = perf_mmap__read_event(&md->core)) != NULL) {
			const u32 type = event->header.type;

			if (type == PERF_RECORD_SAMPLE)
				count ++;
		}
		perf_mmap__read_done(&md->core);
	}

	if (count != expect * evlist->core.nr_entries) {
		pr_debug("BPF filter result incorrect, expected %d, got %d samples\n", expect * evlist->core.nr_entries, count);
		goto out_delete_evlist;
	}

	ret = TEST_OK;

out_delete_evlist:
	evlist__delete(evlist);
	return ret;
}

static struct bpf_object *
prepare_bpf(void *obj_buf, size_t obj_buf_sz, const char *name)
{
	struct bpf_object *obj;

	obj = bpf__prepare_load_buffer(obj_buf, obj_buf_sz, name);
	if (IS_ERR(obj)) {
		pr_debug("Compile BPF program failed.\n");
		return NULL;
	}
	return obj;
}

static int __test__bpf(int idx)
{
	int ret;
	void *obj_buf;
	size_t obj_buf_sz;
	struct bpf_object *obj;

	ret = test_llvm__fetch_bpf_obj(&obj_buf, &obj_buf_sz,
				       bpf_testcase_table[idx].prog_id,
				       true, NULL);
	if (ret != TEST_OK || !obj_buf || !obj_buf_sz) {
		pr_debug("Unable to get BPF object, %s\n",
			 bpf_testcase_table[idx].msg_compile_fail);
		if (idx == 0)
			return TEST_SKIP;
		else
			return TEST_FAIL;
	}

	obj = prepare_bpf(obj_buf, obj_buf_sz,
			  bpf_testcase_table[idx].name);
	if ((!!bpf_testcase_table[idx].target_func) != (!!obj)) {
		if (!obj)
			pr_debug("Fail to load BPF object: %s\n",
				 bpf_testcase_table[idx].msg_load_fail);
		else
			pr_debug("Success unexpectedly: %s\n",
				 bpf_testcase_table[idx].msg_load_fail);
		ret = TEST_FAIL;
		goto out;
	}

	if (obj) {
		ret = do_test(obj,
			      bpf_testcase_table[idx].target_func,
			      bpf_testcase_table[idx].expect_result);
		if (ret != TEST_OK)
			goto out;
		if (bpf_testcase_table[idx].pin) {
			int err;

			if (!bpf_fs__mount()) {
				pr_debug("BPF filesystem not mounted\n");
				ret = TEST_FAIL;
				goto out;
			}
			err = mkdir(PERF_TEST_BPF_PATH, 0777);
			if (err && errno != EEXIST) {
				pr_debug("Failed to make perf_test dir: %s\n",
					 strerror(errno));
				ret = TEST_FAIL;
				goto out;
			}
			if (bpf_object__pin(obj, PERF_TEST_BPF_PATH))
				ret = TEST_FAIL;
			if (rm_rf(PERF_TEST_BPF_PATH))
				ret = TEST_FAIL;
		}
	}

out:
	free(obj_buf);
	bpf__clear();
	return ret;
}

static int check_env(void)
{
	int err;
	unsigned int kver_int;
	char license[] = "GPL";

	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	};

	err = fetch_kernel_version(&kver_int, NULL, 0);
	if (err) {
		pr_debug("Unable to get kernel version\n");
		return err;
	}

/* temporarily disable libbpf deprecation warnings */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	err = bpf_load_program(BPF_PROG_TYPE_KPROBE, insns,
			       ARRAY_SIZE(insns),
			       license, kver_int, NULL, 0);
#pragma GCC diagnostic pop
	if (err < 0) {
		pr_err("Missing basic BPF support, skip this test: %s\n",
		       strerror(errno));
		return err;
	}
	close(err);

	return 0;
}

static int test__bpf(int i)
{
	int err;

	if (i < 0 || i >= (int)ARRAY_SIZE(bpf_testcase_table))
		return TEST_FAIL;

	if (geteuid() != 0) {
		pr_debug("Only root can run BPF test\n");
		return TEST_SKIP;
	}

	if (check_env())
		return TEST_SKIP;

	err = __test__bpf(i);
	return err;
}
#endif

static int test__basic_bpf_test(struct test_suite *test __maybe_unused,
				int subtest __maybe_unused)
{
#ifdef HAVE_LIBBPF_SUPPORT
	return test__bpf(0);
#else
	pr_debug("Skip BPF test because BPF support is not compiled\n");
	return TEST_SKIP;
#endif
}

static int test__bpf_pinning(struct test_suite *test __maybe_unused,
			     int subtest __maybe_unused)
{
#ifdef HAVE_LIBBPF_SUPPORT
	return test__bpf(1);
#else
	pr_debug("Skip BPF test because BPF support is not compiled\n");
	return TEST_SKIP;
#endif
}

static int test__bpf_prologue_test(struct test_suite *test __maybe_unused,
				   int subtest __maybe_unused)
{
#if defined(HAVE_LIBBPF_SUPPORT) && defined(HAVE_BPF_PROLOGUE)
	return test__bpf(2);
#else
	pr_debug("Skip BPF test because BPF support is not compiled\n");
	return TEST_SKIP;
#endif
}


static struct test_case bpf_tests[] = {
#ifdef HAVE_LIBBPF_SUPPORT
	TEST_CASE("Basic BPF filtering", basic_bpf_test),
	TEST_CASE("BPF pinning", bpf_pinning),
#ifdef HAVE_BPF_PROLOGUE
	TEST_CASE("BPF prologue generation", bpf_prologue_test),
#else
	TEST_CASE_REASON("BPF prologue generation", bpf_prologue_test, "not compiled in"),
#endif
#else
	TEST_CASE_REASON("Basic BPF filtering", basic_bpf_test, "not compiled in"),
	TEST_CASE_REASON("BPF pinning", bpf_pinning, "not compiled in"),
	TEST_CASE_REASON("BPF prologue generation", bpf_prologue_test, "not compiled in"),
#endif
	{ .name = NULL, }
};

struct test_suite suite__bpf = {
	.desc = "BPF filter",
	.test_cases = bpf_tests,
};
