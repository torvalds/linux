// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <bpf/btf.h>
#include <search.h>
#include "bpf/libbpf_internal.h"
#include "tracing_multi.skel.h"
#include "tracing_multi_module.skel.h"
#include "tracing_multi_intersect.skel.h"
#include "tracing_multi_session.skel.h"
#include "tracing_multi_fail.skel.h"
#include "tracing_multi_verifier.skel.h"
#include "tracing_multi_bench.skel.h"
#include "tracing_multi_rollback.skel.h"
#include "trace_helpers.h"

static __u64 bpf_fentry_test_cookies[] = {
	8,  /* bpf_fentry_test1 */
	9,  /* bpf_fentry_test2 */
	7,  /* bpf_fentry_test3 */
	5,  /* bpf_fentry_test4 */
	4,  /* bpf_fentry_test5 */
	2,  /* bpf_fentry_test6 */
	3,  /* bpf_fentry_test7 */
	1,  /* bpf_fentry_test8 */
	10, /* bpf_fentry_test9 */
	6,  /* bpf_fentry_test10 */
};

static const char * const bpf_fentry_test[] = {
	"bpf_fentry_test1",
	"bpf_fentry_test2",
	"bpf_fentry_test3",
	"bpf_fentry_test4",
	"bpf_fentry_test5",
	"bpf_fentry_test6",
	"bpf_fentry_test7",
	"bpf_fentry_test8",
	"bpf_fentry_test9",
	"bpf_fentry_test10",
};

static const char * const bpf_testmod_fentry_test[] = {
	"bpf_testmod_fentry_test1",
	"bpf_testmod_fentry_test2",
	"bpf_testmod_fentry_test3",
	"bpf_testmod_fentry_test7",
	"bpf_testmod_fentry_test11",
};

#define FUNCS_CNT (ARRAY_SIZE(bpf_fentry_test))

static int get_random_funcs(const char **funcs)
{
	int i, cnt = 0;

	for (i = 0; i < FUNCS_CNT; i++) {
		if (rand() % 2)
			funcs[cnt++] = bpf_fentry_test[i];
	}
	/* we always need at least one.. */
	if (!cnt)
		funcs[cnt++] = bpf_fentry_test[rand() % FUNCS_CNT];
	return cnt;
}

static int compare(const void *ppa, const void *ppb)
{
	const char *pa = *(const char **) ppa;
	const char *pb = *(const char **) ppb;

	return strcmp(pa, pb);
}

static void tdestroy_free_nop(void *ptr)
{
}

static __u32 *get_ids(const char * const funcs[], int funcs_cnt, const char *mod)
{
	struct btf *btf, *vmlinux_btf = NULL;
	__u32 nr, type_id, cnt = 0;
	void *root = NULL;
	__u32 *ids = NULL;
	int i, err = 0;

	btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(btf, "btf__load_vmlinux_btf"))
		return NULL;

	if (mod) {
		vmlinux_btf = btf;
		btf = btf__load_module_btf(mod, vmlinux_btf);
		if (!ASSERT_OK_PTR(btf, "btf__load_module_btf")) {
			btf__free(vmlinux_btf);
			return NULL;
		}
	}

	ids = calloc(funcs_cnt, sizeof(ids[0]));
	if (!ids)
		goto out;

	/*
	 * We sort function names by name and search them
	 * below for each function.
	 */
	for (i = 0; i < funcs_cnt; i++) {
		if (!tsearch(&funcs[i], &root, compare)) {
			ASSERT_FAIL("tsearch failed");
			err = -1;
			goto error;
		}
	}

	nr = btf__type_cnt(btf);
	for (type_id = 1; type_id < nr && cnt < funcs_cnt; type_id++) {
		const struct btf_type *type;
		const char *str, ***val;
		unsigned int idx;

		type = btf__type_by_id(btf, type_id);
		if (!type) {
			err = -1;
			break;
		}

		if (BTF_INFO_KIND(type->info) != BTF_KIND_FUNC)
			continue;

		str = btf__name_by_offset(btf, type->name_off);
		if (!str) {
			err = -1;
			break;
		}

		val = tfind(&str, &root, compare);
		if (!val)
			continue;

		/*
		 * We keep pointer for each function name so we can get the original
		 * array index and have the resulting ids array matching the original
		 * function array.
		 *
		 * Doing it this way allow us to easily test the cookies support,
		 * because each cookie is attached to particular function/id.
		 */
		idx = *val - funcs;
		ids[idx] = type_id;
		cnt++;
	}

error:
	if (err) {
		free(ids);
		ids = NULL;
	}

out:
	tdestroy(root, tdestroy_free_nop);
	btf__free(vmlinux_btf);
	btf__free(btf);
	return ids;
}

static void tracing_multi_test_run(struct tracing_multi *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	prog_fd = bpf_program__fd(skel->progs.test_fentry);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	/* extra +1 count for sleepable programs */
	ASSERT_EQ(skel->bss->test_result_fentry, FUNCS_CNT + 1, "test_result_fentry");
	ASSERT_EQ(skel->bss->test_result_fexit, FUNCS_CNT + 1, "test_result_fexit");
}

static void test_skel_api(void)
{
	struct tracing_multi *skel;
	int err;

	skel = tracing_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	skel->bss->pid = getpid();

	err = tracing_multi__attach(skel);
	if (!ASSERT_OK(err, "tracing_multi__attach"))
		goto cleanup;

	tracing_multi_test_run(skel);

cleanup:
	tracing_multi__destroy(skel);
}

static void test_link_api_pattern(void)
{
	struct tracing_multi *skel;

	skel = tracing_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	skel->bss->pid = getpid();

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
					"bpf_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
					"bpf_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fentry_s = bpf_program__attach_tracing_multi(skel->progs.test_fentry_s,
					"bpf_fentry_test1", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fentry_s, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit_s = bpf_program__attach_tracing_multi(skel->progs.test_fexit_s,
					"bpf_fentry_test1", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fexit_s, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	tracing_multi_test_run(skel);

cleanup:
	tracing_multi__destroy(skel);
}

static void test_link_api_ids(bool test_cookies)
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	struct tracing_multi *skel;
	size_t cnt = FUNCS_CNT;
	__u32 *ids;

	skel = tracing_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	skel->bss->pid = getpid();
	skel->bss->test_cookies = test_cookies;

	ids = get_ids(bpf_fentry_test, cnt, NULL);
	if (!ASSERT_OK_PTR(ids, "get_ids"))
		goto cleanup;

	opts.ids = ids;
	opts.cnt = cnt;

	if (test_cookies)
		opts.cookies = bpf_fentry_test_cookies;

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	/* Only bpf_fentry_test1 is allowed for sleepable programs. */
	opts.cnt = 1;
	skel->links.test_fentry_s = bpf_program__attach_tracing_multi(skel->progs.test_fentry_s,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fentry_s, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit_s = bpf_program__attach_tracing_multi(skel->progs.test_fexit_s,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fexit_s, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	tracing_multi_test_run(skel);

cleanup:
	tracing_multi__destroy(skel);
	free(ids);
}

static void test_module_skel_api(void)
{
	struct tracing_multi_module *skel = NULL;
	int err;

	skel = tracing_multi_module__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	skel->bss->pid = getpid();

	err = tracing_multi_module__attach(skel);
	if (!ASSERT_OK(err, "tracing_multi__attach"))
		goto cleanup;

	ASSERT_OK(trigger_module_test_read(1), "trigger_read");
	ASSERT_EQ(skel->bss->test_result_fentry, 5, "test_result_fentry");
	ASSERT_EQ(skel->bss->test_result_fexit, 5, "test_result_fexit");

cleanup:
	tracing_multi_module__destroy(skel);
}

static void test_module_link_api_pattern(void)
{
	struct tracing_multi_module *skel = NULL;

	skel = tracing_multi_module__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_module__open_and_load"))
		return;

	skel->bss->pid = getpid();

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
					"bpf_testmod:bpf_testmod_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
					"bpf_testmod:bpf_testmod_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	ASSERT_OK(trigger_module_test_read(1), "trigger_read");
	ASSERT_EQ(skel->bss->test_result_fentry, 5, "test_result_fentry");
	ASSERT_EQ(skel->bss->test_result_fexit, 5, "test_result_fexit");

cleanup:
	tracing_multi_module__destroy(skel);
}

static void test_module_link_api_ids(void)
{
	size_t cnt = ARRAY_SIZE(bpf_testmod_fentry_test);
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	struct tracing_multi_module *skel = NULL;
	__u32 *ids;

	skel = tracing_multi_module__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_module__open_and_load"))
		return;

	skel->bss->pid = getpid();

	ids = get_ids(bpf_testmod_fentry_test, cnt, "bpf_testmod");
	if (!ASSERT_OK_PTR(ids, "get_ids"))
		goto cleanup;

	opts.ids = ids;
	opts.cnt = cnt;

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	ASSERT_OK(trigger_module_test_read(1), "trigger_read");
	ASSERT_EQ(skel->bss->test_result_fentry, 5, "test_result_fentry");
	ASSERT_EQ(skel->bss->test_result_fexit, 5, "test_result_fexit");

cleanup:
	tracing_multi_module__destroy(skel);
	free(ids);
}

static bool is_set(__u32 mask, __u32 bit)
{
	return (1 << bit) & mask;
}

static void __test_intersect(__u32 mask, const struct bpf_program *progs[4], __u64 *test_results[4])
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct bpf_link *links[4] = { NULL };
	const char *funcs[FUNCS_CNT];
	__u64 expected[4];
	__u32 *ids, i;
	int err, cnt;

	/*
	 * We have 4 programs in progs and the mask bits pick which
	 * of them gets attached to randomly chosen functions.
	 */
	for (i = 0; i < 4; i++) {
		if (!is_set(mask, i))
			continue;

		cnt = get_random_funcs(funcs);
		ids = get_ids(funcs, cnt, NULL);
		if (!ASSERT_OK_PTR(ids, "get_ids"))
			goto cleanup;

		opts.ids = ids;
		opts.cnt = cnt;
		links[i] = bpf_program__attach_tracing_multi(progs[i], NULL, &opts);
		free(ids);

		if (!ASSERT_OK_PTR(links[i], "bpf_program__attach_tracing_multi"))
			goto cleanup;

		expected[i] = *test_results[i] + cnt;
	}

	err = bpf_prog_test_run_opts(bpf_program__fd(progs[0]), &topts);
	ASSERT_OK(err, "test_run");

	for (i = 0; i < 4; i++) {
		if (!is_set(mask, i))
			continue;
		ASSERT_EQ(*test_results[i], expected[i], "test_results");
	}

cleanup:
	for (i = 0; i < 4; i++)
		bpf_link__destroy(links[i]);
}

static void test_intersect(void)
{
	struct tracing_multi_intersect *skel;
	const struct bpf_program *progs[4];
	__u64 *test_results[4];
	__u32 i;

	skel = tracing_multi_intersect__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_intersect__open_and_load"))
		return;

	skel->bss->pid = getpid();

	progs[0] = skel->progs.fentry_1;
	progs[1] = skel->progs.fexit_1;
	progs[2] = skel->progs.fentry_2;
	progs[3] = skel->progs.fexit_2;

	test_results[0] = &skel->bss->test_result_fentry_1;
	test_results[1] = &skel->bss->test_result_fexit_1;
	test_results[2] = &skel->bss->test_result_fentry_2;
	test_results[3] = &skel->bss->test_result_fexit_2;

	for (i = 1; i < 16; i++)
		__test_intersect(i, progs, test_results);

	tracing_multi_intersect__destroy(skel);
}

static void test_session(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct tracing_multi_session *skel;
	int err, prog_fd;

	skel = tracing_multi_session__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_session__open_and_load"))
		return;

	skel->bss->pid = getpid();

	err = tracing_multi_session__attach(skel);
	if (!ASSERT_OK(err, "tracing_multi_session__attach"))
		goto cleanup;

	/* execute kernel session */
	prog_fd = bpf_program__fd(skel->progs.test_session_1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	/* 10 for test_session_1, 1 for test_fsession_s */
	ASSERT_EQ(skel->bss->test_result_fentry, 11, "test_result_fentry");
	/* extra count (+1 for each fexit execution) for test_result_fexit cookie check/inc */
	ASSERT_EQ(skel->bss->test_result_fexit, 22, "test_result_fexit");

	skel->bss->test_result_fentry = 0;
	skel->bss->test_result_fexit = 0;

	/* execute bpf_testmo.ko session */
	ASSERT_OK(trigger_module_test_read(1), "trigger_read");

	/* 5 for test_session_2 */
	ASSERT_EQ(skel->bss->test_result_fentry, 5, "test_result_fentry");
	/* extra count (+1 for each fexit execution) for test_result_fexit cookie */
	ASSERT_EQ(skel->bss->test_result_fexit, 10, "test_result_fexit");


cleanup:
	tracing_multi_session__destroy(skel);
}

static void test_attach_api_fails(void)
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	static const char * const func[] = {
		"bpf_fentry_test2",
	};
	struct tracing_multi_fail *skel = NULL;
	__u32 ids[2] = {}, *ids2 = NULL;
	__u64 cookies[2];

	skel = tracing_multi_fail__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_fail__open_and_load"))
		return;

	/* fail#1 (libbpf) pattern and opts NULL */
	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						NULL, NULL);
	if (!ASSERT_EQ(libbpf_get_error(skel->links.test_fentry), -EINVAL, "fail_1"))
		goto cleanup;

	/* fail#2 (libbpf) pattern and ids */
	LIBBPF_OPTS_RESET(opts,
		.ids = ids,
		.cnt = 2,
	);

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						"bpf_fentry_test*", &opts);
	if (!ASSERT_EQ(libbpf_get_error(skel->links.test_fentry), -EINVAL, "fail_2"))
		goto cleanup;

	/* fail#3 (libbpf) pattern and cookies */
	LIBBPF_OPTS_RESET(opts,
		.ids = NULL,
		.cnt = 2,
		.cookies = cookies,
	);

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						"bpf_fentry_test*", &opts);
	if (!ASSERT_EQ(libbpf_get_error(skel->links.test_fentry), -EINVAL, "fail_3"))
		goto cleanup;

	/* fail#4 (libbpf) bogus pattern */
	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						"bpf_not_really_a_function*", NULL);
	if (!ASSERT_EQ(libbpf_get_error(skel->links.test_fentry), -EINVAL, "fail_4"))
		goto cleanup;

	/* fail#5 (kernel) abnormal cnt */
	LIBBPF_OPTS_RESET(opts,
		.ids = ids,
		.cnt = INT_MAX,
	);

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						NULL, &opts);
	if (!ASSERT_EQ(libbpf_get_error(skel->links.test_fentry), -E2BIG, "fail_5"))
		goto cleanup;

	/* fail#6 (kernel) attach sleepable program to not-allowed function */
	ids2 = get_ids(func, 1, NULL);
	if (!ASSERT_OK_PTR(ids2, "get_ids"))
		goto cleanup;

	LIBBPF_OPTS_RESET(opts,
		.ids = ids2,
		.cnt = 1,
	);

	skel->links.test_fentry_s = bpf_program__attach_tracing_multi(skel->progs.test_fentry_s,
						NULL, &opts);
	if (!ASSERT_EQ(libbpf_get_error(skel->links.test_fentry_s), -EINVAL, "fail_6"))
		goto cleanup;

	/* fail#7 (kernel) attach with duplicate id */
	ids[0] = ids2[0];
	ids[1] = ids2[0];

	LIBBPF_OPTS_RESET(opts,
		.ids = ids,
		.cnt = 2,
	);

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						NULL, &opts);
	ASSERT_EQ(libbpf_get_error(skel->links.test_fentry), -EINVAL, "fail_7");

cleanup:
	tracing_multi_fail__destroy(skel);
	free(ids2);
}

void serial_test_tracing_multi_bench_attach(void)
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	struct tracing_multi_bench *skel = NULL;
	long attach_start_ns, attach_end_ns;
	long detach_start_ns, detach_end_ns;
	double attach_delta, detach_delta;
	struct bpf_link *link = NULL;
	size_t i, cap = 0, cnt = 0;
	struct ksyms *ksyms = NULL;
	void *root = NULL;
	void *dups = NULL;
	__u32 *ids = NULL;
	__u32 nr, type_id;
	struct btf *btf;
	int err;

#ifndef __x86_64__
	test__skip();
	return;
#endif

	btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(btf, "btf__load_vmlinux_btf"))
		return;

	skel = tracing_multi_bench__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_bench__open_and_load"))
		goto cleanup;

	if (!ASSERT_OK(bpf_get_ksyms(&ksyms, true), "get_syms"))
		goto cleanup;

	/* Get all ftrace 'safe' symbols.. */
	for (i = 0; i < ksyms->filtered_cnt; i++) {
		if (!tsearch(&ksyms->filtered_syms[i], &root, compare)) {
			ASSERT_FAIL("tsearch failed");
			goto cleanup;
		}
	}

	/*
	 * Collect names that are not unique in kallsyms. The kernel resolves a
	 * tracing-multi BTF id to an address with kallsyms_lookup_name(), which
	 * returns the first symbol of that name. For a duplicate name that may
	 * be a different (non-ftrace-able) instance than the ftrace-able one in
	 * available_filter_functions, so attaching to it by BTF id fails with
	 * -ENOENT (e.g. t_start/t_next/t_stop). ksyms->syms is sorted by name,
	 * so equal names are adjacent.
	 */
	for (i = 1; i < ksyms->sym_cnt; i++) {
		if (strcmp(ksyms->syms[i].name, ksyms->syms[i - 1].name))
			continue;
		if (!tsearch(&ksyms->syms[i].name, &dups, compare)) {
			ASSERT_FAIL("tsearch failed");
			goto cleanup;
		}
	}

	/* ..and filter them through BTF and btf_type_is_traceable_func. */
	nr = btf__type_cnt(btf);
	for (type_id = 1; type_id < nr; type_id++) {
		const struct btf_type *type;
		const char *str;

		type = btf__type_by_id(btf, type_id);
		if (!type)
			break;

		if (BTF_INFO_KIND(type->info) != BTF_KIND_FUNC)
			continue;

		str = btf__name_by_offset(btf, type->name_off);
		if (!str)
			break;

		if (!tfind(&str, &root, compare))
			continue;

		/* Skip names that are not unique in kallsyms, see above. */
		if (tfind(&str, &dups, compare))
			continue;

		if (!btf_type_is_traceable_func(btf, type))
			continue;

		err = libbpf_ensure_mem((void **) &ids, &cap, sizeof(*ids), cnt + 1);
		if (err)
			goto cleanup;

		ids[cnt++] = type_id;
	}

	opts.ids = ids;
	opts.cnt = cnt;

	attach_start_ns = get_time_ns();
	link = bpf_program__attach_tracing_multi(skel->progs.bench, NULL, &opts);
	attach_end_ns = get_time_ns();

	if (!ASSERT_OK_PTR(link, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	detach_start_ns = get_time_ns();
	bpf_link__destroy(link);
	detach_end_ns = get_time_ns();

	attach_delta = (attach_end_ns - attach_start_ns) / 1000000000.0;
	detach_delta = (detach_end_ns - detach_start_ns) / 1000000000.0;

	printf("%s: found %lu functions\n", __func__, cnt);
	printf("%s: attached in %7.3lfs\n", __func__, attach_delta);
	printf("%s: detached in %7.3lfs\n", __func__, detach_delta);

cleanup:
	tracing_multi_bench__destroy(skel);
	tdestroy(root, tdestroy_free_nop);
	tdestroy(dups, tdestroy_free_nop);
	free_kallsyms_local(ksyms);
	free(ids);
	btf__free(btf);
}

static void tracing_multi_rollback_run(struct tracing_multi_rollback *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	prog_fd = bpf_program__fd(skel->progs.test_fentry);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	/* make sure the rollback code did not leave any program attached */
	ASSERT_EQ(skel->bss->test_result_fentry, 0, "test_result_fentry");
	ASSERT_EQ(skel->bss->test_result_fexit, 0, "test_result_fexit");
}

static void test_rollback_put(void)
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	struct tracing_multi_rollback *skel = NULL;
	size_t cnt = FUNCS_CNT;
	__u32 *ids = NULL;
	int err;

	skel = tracing_multi_rollback__open();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_rollback__open"))
		return;

	bpf_program__set_autoload(skel->progs.test_fentry, true);
	bpf_program__set_autoload(skel->progs.test_fexit, true);

	err = tracing_multi_rollback__load(skel);
	if (!ASSERT_OK(err, "tracing_multi_rollback__load"))
		goto cleanup;

	ids = get_ids(bpf_fentry_test, cnt, NULL);
	if (!ASSERT_OK_PTR(ids, "get_ids"))
		goto cleanup;

	/*
	 * Mangle last id to trigger rollback, which needs to do put
	 * on get-ed trampolines.
	 */
	ids[9] = 0;

	opts.ids = ids;
	opts.cnt = cnt;

	skel->bss->pid = getpid();

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						NULL, &opts);
	if (!ASSERT_ERR_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
						NULL, &opts);
	if (!ASSERT_ERR_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	/* We don't really attach any program, but let's make sure. */
	tracing_multi_rollback_run(skel);

cleanup:
	tracing_multi_rollback__destroy(skel);
	free(ids);
}

static void fillers_cleanup(struct tracing_multi_rollback **skels, int cnt)
{
	int i;

	for (i = 0; i < cnt; i++)
		tracing_multi_rollback__destroy(skels[i]);

	free(skels);
}

static struct tracing_multi_rollback *extra_load_and_link(void)
{
	struct tracing_multi_rollback *skel;
	int err;

	skel = tracing_multi_rollback__open();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_rollback__open"))
		goto cleanup;

	bpf_program__set_autoload(skel->progs.extra, true);

	err = tracing_multi_rollback__load(skel);
	if (!ASSERT_OK(err, "tracing_multi_rollback__load"))
		goto cleanup;

	skel->links.extra = bpf_program__attach_trace(skel->progs.extra);
	if (!ASSERT_OK_PTR(skel->links.extra, "bpf_program__attach_trace"))
		goto cleanup;

	return skel;

cleanup:
	tracing_multi_rollback__destroy(skel);
	return NULL;
}

static struct tracing_multi_rollback **fillers_load_and_link(int max)
{
	struct tracing_multi_rollback **skels, *skel;
	int i, err;

	skels = calloc(max + 1, sizeof(*skels));
	if (!ASSERT_OK_PTR(skels, "calloc"))
		return NULL;

	for (i = 0; i < max; i++) {
		skel = skels[i] = tracing_multi_rollback__open();
		if (!ASSERT_OK_PTR(skels[i], "tracing_multi_rollback__open"))
			goto cleanup;

		bpf_program__set_autoload(skel->progs.filler, true);

		err = tracing_multi_rollback__load(skel);
		if (!ASSERT_OK(err, "tracing_multi_rollback__load"))
			goto cleanup;

		skel->links.filler = bpf_program__attach_trace(skel->progs.filler);
		if (!ASSERT_OK_PTR(skels[i]->links.filler, "bpf_program__attach_trace"))
			goto cleanup;
	}

	return skels;

cleanup:
	fillers_cleanup(skels, i + 1);
	return NULL;
}

static void test_rollback_unlink(void)
{
	struct tracing_multi_rollback *skel = NULL, *extra;
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	struct tracing_multi_rollback **fillers;
	size_t cnt = FUNCS_CNT;
	__u32 *ids = NULL;
	int err, max;

	max = get_bpf_max_tramp_links();
	if (!ASSERT_GE(max, 1, "bpf_max_tramp_links"))
		return;

	/* Attach maximum allowed programs to bpf_fentry_test10 */
	fillers = fillers_load_and_link(max);
	if (!ASSERT_OK_PTR(fillers, "fillers_load_and_link"))
		return;

	extra = extra_load_and_link();
	if (!ASSERT_OK_PTR(extra, "extra_load_and_link"))
		goto cleanup;

	skel = tracing_multi_rollback__open();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_rollback__open"))
		goto cleanup;

	bpf_program__set_autoload(skel->progs.test_fentry, true);
	bpf_program__set_autoload(skel->progs.test_fexit, true);

	/*
	 * Attach tracing_multi link on bpf_fentry_test1-10, which will
	 * fail on bpf_fentry_test10 function, because it already has
	 * maximum allowed programs attached.
	 *
	 * The rollback needs to unlink already link-ed trampolines and
	 * put all of them.
	 */
	err = tracing_multi_rollback__load(skel);
	if (!ASSERT_OK(err, "tracing_multi_rollback__load"))
		goto cleanup;

	ids = get_ids(bpf_fentry_test, cnt, NULL);
	if (!ASSERT_OK_PTR(ids, "get_ids"))
		goto cleanup;

	opts.ids = ids;
	opts.cnt = cnt;

	skel->bss->pid = getpid();

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						NULL, &opts);
	if (!ASSERT_ERR_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
						NULL, &opts);
	if (!ASSERT_ERR_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	tracing_multi_rollback_run(skel);

cleanup:
	fillers_cleanup(fillers, max);
	tracing_multi_rollback__destroy(extra);
	tracing_multi_rollback__destroy(skel);
	free(ids);
}

void serial_test_tracing_multi_attach_rollback(void)
{
	if (test__start_subtest("put"))
		test_rollback_put();
	if (test__start_subtest("unlink"))
		test_rollback_unlink();
}

void test_tracing_multi_test(void)
{
#ifndef __x86_64__
	test__skip();
	return;
#endif

	if (test__start_subtest("skel_api"))
		test_skel_api();
	if (test__start_subtest("link_api_pattern"))
		test_link_api_pattern();
	if (test__start_subtest("link_api_ids"))
		test_link_api_ids(false);
	if (test__start_subtest("module_skel_api"))
		test_module_skel_api();
	if (test__start_subtest("module_link_api_pattern"))
		test_module_link_api_pattern();
	if (test__start_subtest("module_link_api_ids"))
		test_module_link_api_ids();
	if (test__start_subtest("intersect"))
		test_intersect();
	if (test__start_subtest("cookies"))
		test_link_api_ids(true);
	if (test__start_subtest("session"))
		test_session();
	if (test__start_subtest("attach_api_fails"))
		test_attach_api_fails();
	RUN_TESTS(tracing_multi_verifier);
}
