#include <stdio.h>
#include <sys/epoll.h>
#include <util/bpf-loader.h>
#include <util/evlist.h>
#include "tests.h"
#include "llvm.h"
#include "debug.h"
#define NR_ITERS       111

#ifdef HAVE_LIBBPF_SUPPORT

static int epoll_pwait_loop(void)
{
	int i;

	/* Should fail NR_ITERS times */
	for (i = 0; i < NR_ITERS; i++)
		epoll_pwait(-(i + 1), NULL, 0, 0, NULL);
	return 0;
}

static struct {
	enum test_llvm__testcase prog_id;
	const char *desc;
	const char *name;
	const char *msg_compile_fail;
	const char *msg_load_fail;
	int (*target_func)(void);
	int expect_result;
} bpf_testcase_table[] = {
	{
		LLVM_TESTCASE_BASE,
		"Test basic BPF filtering",
		"[basic_bpf_test]",
		"fix 'perf test LLVM' first",
		"load bpf object failed",
		&epoll_pwait_loop,
		(NR_ITERS + 1) / 2,
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
	struct perf_evlist *evlist;
	int i, ret = TEST_FAIL, err = 0, count = 0;

	struct parse_events_evlist parse_evlist;
	struct parse_events_error parse_error;

	bzero(&parse_error, sizeof(parse_error));
	bzero(&parse_evlist, sizeof(parse_evlist));
	parse_evlist.error = &parse_error;
	INIT_LIST_HEAD(&parse_evlist.list);

	err = parse_events_load_bpf_obj(&parse_evlist, &parse_evlist.list, obj);
	if (err || list_empty(&parse_evlist.list)) {
		pr_debug("Failed to add events selected by BPF\n");
		if (!err)
			return TEST_FAIL;
	}

	snprintf(pid, sizeof(pid), "%d", getpid());
	pid[sizeof(pid) - 1] = '\0';
	opts.target.tid = opts.target.pid = pid;

	/* Instead of perf_evlist__new_default, don't add default events */
	evlist = perf_evlist__new();
	if (!evlist) {
		pr_debug("No ehough memory to create evlist\n");
		return TEST_FAIL;
	}

	err = perf_evlist__create_maps(evlist, &opts.target);
	if (err < 0) {
		pr_debug("Not enough memory to create thread/cpu maps\n");
		goto out_delete_evlist;
	}

	perf_evlist__splice_list_tail(evlist, &parse_evlist.list);
	evlist->nr_groups = parse_evlist.nr_groups;

	perf_evlist__config(evlist, &opts);

	err = perf_evlist__open(evlist);
	if (err < 0) {
		pr_debug("perf_evlist__open: %s\n",
			 strerror_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	err = perf_evlist__mmap(evlist, opts.mmap_pages, false);
	if (err < 0) {
		pr_debug("perf_evlist__mmap: %s\n",
			 strerror_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	perf_evlist__enable(evlist);
	(*func)();
	perf_evlist__disable(evlist);

	for (i = 0; i < evlist->nr_mmaps; i++) {
		union perf_event *event;

		while ((event = perf_evlist__mmap_read(evlist, i)) != NULL) {
			const u32 type = event->header.type;

			if (type == PERF_RECORD_SAMPLE)
				count ++;
		}
	}

	if (count != expect)
		pr_debug("BPF filter result incorrect\n");

	ret = TEST_OK;

out_delete_evlist:
	perf_evlist__delete(evlist);
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
				       true);
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
	if (!obj) {
		ret = TEST_FAIL;
		goto out;
	}

	ret = do_test(obj,
		      bpf_testcase_table[idx].target_func,
		      bpf_testcase_table[idx].expect_result);
out:
	bpf__clear();
	return ret;
}

int test__bpf(void)
{
	unsigned int i;
	int err;

	if (geteuid() != 0) {
		pr_debug("Only root can run BPF test\n");
		return TEST_SKIP;
	}

	for (i = 0; i < ARRAY_SIZE(bpf_testcase_table); i++) {
		err = __test__bpf(i);

		if (err != TEST_OK)
			return err;
	}

	return TEST_OK;
}

#else
int test__bpf(void)
{
	pr_debug("Skip BPF test because BPF support is not compiled\n");
	return TEST_SKIP;
}
#endif
