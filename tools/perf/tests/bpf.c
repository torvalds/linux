// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <util/record.h>
#include <util/util.h>
#include <util/bpf-loader.h>
#include <util/evlist.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <api/fs/fs.h>
#include <bpf/bpf.h>
#include <perf/mmap.h>
#include "tests.h"
#include "llvm.h"
#include "debug.h"
#include "parse-events.h"
#include "util/mmap.h"
#define NR_ITERS       111
#define PERF_TEST_BPF_PATH "/sys/fs/bpf/perf_test"

#ifdef HAVE_LIBBPF_SUPPORT

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
	const char *desc;
	const char *name;
	const char *msg_compile_fail;
	const char *msg_load_fail;
	int (*target_func)(void);
	int expect_result;
	bool	pin;
} bpf_testcase_table[] = {
	{
		.prog_id	  = LLVM_TESTCASE_BASE,
		.desc		  = "Basic BPF filtering",
		.name		  = "[basic_bpf_test]",
		.msg_compile_fail = "fix 'perf test LLVM' first",
		.msg_load_fail	  = "load bpf object failed",
		.target_func	  = &epoll_pwait_loop,
		.expect_result	  = (NR_ITERS + 1) / 2,
	},
	{
		.prog_id	  = LLVM_TESTCASE_BASE,
		.desc		  = "BPF pinning",
		.name		  = "[bpf_pinning]",
		.msg_compile_fail = "fix kbuild first",
		.msg_load_fail	  = "check your vmlinux setting?",
		.target_func	  = &epoll_pwait_loop,
		.expect_result	  = (NR_ITERS + 1) / 2,
		.pin 		  = true,
	},
#ifdef HAVE_BPF_PROLOGUE
	{
		.prog_id	  = LLVM_TESTCASE_BPF_PROLOGUE,
		.desc		  = "BPF prologue generation",
		.name		  = "[bpf_prologue_test]",
		.msg_compile_fail = "fix kbuild first",
		.msg_load_fail	  = "check your vmlinux setting?",
		.target_func	  = &llseek_loop,
		.expect_result	  = (NR_ITERS + 1) / 4,
	},
#endif
	{
		.prog_id	  = LLVM_TESTCASE_BPF_RELOCATION,
		.desc		  = "BPF relocation checker",
		.name		  = "[bpf_relocation_test]",
		.msg_compile_fail = "fix 'perf test LLVM' first",
		.msg_load_fail	  = "libbpf error when dealing with relocation",
	},
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

	bzero(&parse_error, sizeof(parse_error));
	bzero(&parse_state, sizeof(parse_state));
	parse_state.error = &parse_error;
	INIT_LIST_HEAD(&parse_state.list);

	err = parse_events_load_bpf_obj(&parse_state, &parse_state.list, obj, NULL);
	if (err || list_empty(&parse_state.list)) {
		pr_debug("Failed to add events selected by BPF\n");
		return TEST_FAIL;
	}

	snprintf(pid, sizeof(pid), "%d", getpid());
	pid[sizeof(pid) - 1] = '\0';
	opts.target.tid = opts.target.pid = pid;

	/* Instead of perf_evlist__new_default, don't add default events */
	evlist = evlist__new();
	if (!evlist) {
		pr_debug("Not enough memory to create evlist\n");
		return TEST_FAIL;
	}

	err = perf_evlist__create_maps(evlist, &opts.target);
	if (err < 0) {
		pr_debug("Not enough memory to create thread/cpu maps\n");
		goto out_delete_evlist;
	}

	perf_evlist__splice_list_tail(evlist, &parse_state.list);
	evlist->nr_groups = parse_state.nr_groups;

	perf_evlist__config(evlist, &opts, NULL);

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

	if (count != expect) {
		pr_debug("BPF filter result incorrect, expected %d, got %d samples\n", expect, count);
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
	bpf__clear();
	return ret;
}

int test__bpf_subtest_get_nr(void)
{
	return (int)ARRAY_SIZE(bpf_testcase_table);
}

const char *test__bpf_subtest_get_desc(int i)
{
	if (i < 0 || i >= (int)ARRAY_SIZE(bpf_testcase_table))
		return NULL;
	return bpf_testcase_table[i].desc;
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

	err = bpf_load_program(BPF_PROG_TYPE_KPROBE, insns,
			       sizeof(insns) / sizeof(insns[0]),
			       license, kver_int, NULL, 0);
	if (err < 0) {
		pr_err("Missing basic BPF support, skip this test: %s\n",
		       strerror(errno));
		return err;
	}
	close(err);

	return 0;
}

int test__bpf(struct test *test __maybe_unused, int i)
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

#else
int test__bpf_subtest_get_nr(void)
{
	return 0;
}

const char *test__bpf_subtest_get_desc(int i __maybe_unused)
{
	return NULL;
}

int test__bpf(struct test *test __maybe_unused, int i __maybe_unused)
{
	pr_debug("Skip BPF test because BPF support is not compiled\n");
	return TEST_SKIP;
}
#endif
